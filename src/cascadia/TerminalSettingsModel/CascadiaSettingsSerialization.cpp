// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "CascadiaSettings.h"

#include <LibraryResources.h>
#include <fmt/chrono.h>
#include <shlobj.h>
#include <til/latch.h>

#include "AzureCloudShellGenerator.h"
#include "PowershellCoreProfileGenerator.h"
#include "WslDistroGenerator.h"

// The following files are generated at build time into the "Generated Files" directory.
// defaults(-universal).h is a file containing the default json settings in a std::string_view.
#include "defaults.h"
#include "defaults-universal.h"
// userDefault.h is like the above, but with a default template for the user's settings.json.
#include <LegacyProfileGeneratorNamespaces.h>

#include "userDefaults.h"

#include "ApplicationState.h"
#include "FileUtils.h"

using namespace winrt::Microsoft::Terminal::Settings;
using namespace winrt::Microsoft::Terminal::Settings::Model::implementation;

static constexpr std::wstring_view SettingsFilename{ L"settings.json" };
static constexpr std::wstring_view DefaultsFilename{ L"defaults.json" };

static constexpr std::string_view ProfilesKey{ "profiles" };
static constexpr std::string_view DefaultSettingsKey{ "defaults" };
static constexpr std::string_view ProfilesListKey{ "list" };
static constexpr std::string_view SchemesKey{ "schemes" };
static constexpr std::string_view NameKey{ "name" };
static constexpr std::string_view GuidKey{ "guid" };

static constexpr std::wstring_view jsonExtension{ L".json" };
static constexpr std::wstring_view FragmentsSubDirectory{ L"\\Fragments" };
static constexpr std::wstring_view FragmentsPath{ L"\\Microsoft\\Windows Terminal\\Fragments" };

static constexpr std::wstring_view AppExtensionHostName{ L"com.microsoft.windows.terminal.settings" };

// make sure this matches defaults.json.
static constexpr winrt::guid DEFAULT_WINDOWS_POWERSHELL_GUID{ 0x61c54bbd, 0xc2c6, 0x5271, { 0x96, 0xe7, 0x00, 0x9a, 0x87, 0xff, 0x44, 0xbf } };
static constexpr winrt::guid DEFAULT_COMMAND_PROMPT_GUID{ 0x0caa0dad, 0x35be, 0x5f56, { 0xa8, 0xff, 0xaf, 0xce, 0xee, 0xaa, 0x61, 0x01 } };

// Function Description:
// - Extracting the value from an async task (like talking to the app catalog) when we are on the
//   UI thread causes C++/WinRT to complain quite loudly (and halt execution!)
//   This templated function extracts the result from a task with chicanery.
template<typename TTask>
static auto extractValueFromTaskWithoutMainThreadAwait(TTask&& task) -> decltype(task.get())
{
    std::optional<decltype(task.get())> finalVal;
    til::latch latch{ 1 };

    const auto _ = [&]() -> winrt::fire_and_forget {
        co_await winrt::resume_background();
        finalVal.emplace(co_await task);
        latch.count_down();
    }();

    latch.wait();
    return finalVal.value();
}

template<typename T>
static void executeGenerator(const std::unordered_set<std::wstring_view>& ignoredNamespaces, std::vector<winrt::com_ptr<Profile>>& generatedProfiles)
{
    T generator;
    const auto generatorNamespace = generator.GetNamespace();

    if (!ignoredNamespaces.count(generatorNamespace))
    {
        try
        {
            generator.GenerateProfiles(generatedProfiles);
        }
        CATCH_LOG_MSG("Dynamic Profile Namespace: \"%s\"", generatorNamespace.data());
    }
}

std::filesystem::path buildPath(const std::wstring_view& lhs, const std::wstring_view& rhs)
{
    std::wstring buffer;
    buffer.reserve(lhs.size() + rhs.size());
    buffer.append(lhs);
    buffer.append(rhs);
    return { std::move(buffer) };
}

SettingsLoader SettingsLoader::Default(const std::string_view& userJSON, const std::string_view& inboxJSON)
{
    SettingsLoader loader{ userJSON, inboxJSON };
    loader.MergeInboxIntoUserProfiles();
    loader.FinalizeLayering();
    return loader;
}

SettingsLoader::SettingsLoader(const std::string_view& userJSON, const std::string_view& inboxJSON)
{
    _parse(OriginTag::InBox, inboxJSON, inboxSettings);

    try
    {
        _parse(OriginTag::User, userJSON, userSettings);
    }
    catch (const JsonUtils::DeserializationError& e)
    {
        _rethrowSerializationExceptionWithLocationInfo(e, userJSON);
    }

    if (const auto sources = userSettings.globals->DisabledProfileSources())
    {
        ignoredNamespaces.reserve(sources.Size());
        for (const auto& id : sources)
        {
            ignoredNamespaces.emplace(id);
        }
    }

    // See member description of userProfileCount.
    userProfileCount = userSettings.profiles.size();
}

// Generate dynamic profiles and add them as parents of user profiles.
// That way the user profiles will get appropriate defaults from the generators (like icons and such).
void SettingsLoader::GenerateProfiles()
{
    const auto executeGenerator = [&](const auto& generator) {
        const auto generatorNamespace = generator.GetNamespace();

        if (!ignoredNamespaces.count(generatorNamespace))
        {
            try
            {
                generator.GenerateProfiles(inboxSettings.profiles);
            }
            CATCH_LOG_MSG("Dynamic Profile Namespace: \"%.*s\"", generatorNamespace.data(), generatorNamespace.size())
        }
    };

    executeGenerator(PowershellCoreProfileGenerator{});
    executeGenerator(WslDistroGenerator{});
    executeGenerator(AzureCloudShellGenerator{});
}

// A new settings.json gets a special treatment:
// 1. The default profile is a PowerShell 7+ one, if one was generated,
//    and falls back to the standard PowerShell 5 profile otherwise.
// 2. cmd.exe gets a localized name.
void SettingsLoader::FillBlanksInDefaultsJson()
{
    // 1.
    {
        const auto preferredPowershellProfile = PowershellCoreProfileGenerator::GetPreferredPowershellProfileName();
        auto guid = DEFAULT_WINDOWS_POWERSHELL_GUID;

        for (const auto& profile : inboxSettings.profiles)
        {
            if (profile->Name() == preferredPowershellProfile)
            {
                guid = profile->Guid();
                break;
            }
        }

        userSettings.globals->DefaultProfile(guid);
    }

    // 2.
    {
        for (const auto& profile : userSettings.profiles)
        {
            if (profile->Guid() == DEFAULT_COMMAND_PROMPT_GUID)
            {
                profile->Name(RS_(L"CommandPromptDisplayName"));
                break;
            }
        }
    }
}

void SettingsLoader::MergeInboxIntoUserProfiles()
{
    for (const auto& generatedProfile : inboxSettings.profiles)
    {
        if (const auto [it, inserted] = userSettings.profilesByGuid.emplace(generatedProfile->Guid(), generatedProfile); !inserted)
        {
            it->second->InsertParent(generatedProfile);
        }
        else
        {
            userSettings.profiles.emplace_back(ReproduceProfile(generatedProfile));
        }
    }
}

void SettingsLoader::MergeFragmentsIntoUserProfiles()
{
    ParsedSettings fragmentSettings;

    const auto parseAndLayerFragmentFiles = [&](const std::filesystem::path& path, const winrt::hstring& source) {
        for (const auto& fragmentExt : std::filesystem::directory_iterator{ path })
        {
            if (fragmentExt.path().extension() == jsonExtension)
            {
                try
                {
                    const auto content = ReadUTF8File(fragmentExt.path());
                    _parse(OriginTag::Fragment, content, fragmentSettings);

                    for (const auto& fragmentProfile : fragmentSettings.profiles)
                    {
                        if (const auto updates = fragmentProfile->Updates(); updates != winrt::guid{})
                        {
                            if (const auto it = userSettings.profilesByGuid.find(updates); it != userSettings.profilesByGuid.end())
                            {
                                fragmentProfile->Source(source);
                                it->second->InsertParent(0, fragmentProfile);
                            }
                        }
                        else
                        {
                            // TODO: GUID uniqueness?
                            fragmentProfile->Source(source);
                            _appendProfile(ReproduceProfile(fragmentProfile), userSettings);
                        }
                    }

                    for (const auto& kv : fragmentSettings.globals->ColorSchemes())
                    {
                        userSettings.globals->AddColorScheme(kv.Value());
                    }
                }
                CATCH_LOG();
            }
        }
    };

    for (const auto& rfid : std::array{ FOLDERID_LocalAppData, FOLDERID_ProgramData })
    {
        wil::unique_cotaskmem_string folder;
        THROW_IF_FAILED(SHGetKnownFolderPath(rfid, 0, nullptr, &folder));

        const auto fragmentPath = buildPath(folder.get(), FragmentsPath);

        if (std::filesystem::is_directory(fragmentPath))
        {
            for (const auto& fragmentExtFolder : std::filesystem::directory_iterator{ fragmentPath })
            {
                const auto filename = fragmentExtFolder.path().filename();
                const auto& source = filename.native();

                if (!ignoredNamespaces.count(std::wstring_view{ source }) && fragmentExtFolder.is_directory())
                {
                    parseAndLayerFragmentFiles(fragmentExtFolder.path(), winrt::hstring{ source });
                }
            }
        }
    }

    // Search through app extensions
    // Gets the catalog of extensions with the name "com.microsoft.windows.terminal.settings"
    const auto catalog = winrt::Windows::ApplicationModel::AppExtensions::AppExtensionCatalog::Open(AppExtensionHostName);
    const auto extensions = extractValueFromTaskWithoutMainThreadAwait(catalog.FindAllAsync());

    for (const auto& ext : extensions)
    {
        const auto packageName = ext.Package().Id().FamilyName();
        if (ignoredNamespaces.count(std::wstring_view{ packageName }))
        {
            continue;
        }

        // Likewise, getting the public folder from an extension is an async operation.
        auto foundFolder = extractValueFromTaskWithoutMainThreadAwait(ext.GetPublicFolderAsync());
        if (!foundFolder)
        {
            continue;
        }

        // the StorageFolder class has its own methods for obtaining the files within the folder
        // however, all those methods are Async methods
        // you may have noticed that we need to resort to clunky implementations for async operations
        // (they are in extractValueFromTaskWithoutMainThreadAwait)
        // so for now we will just take the folder path and access the files that way
        const auto path = buildPath(foundFolder.Path(), FragmentsSubDirectory);

        if (std::filesystem::is_directory(path))
        {
            parseAndLayerFragmentFiles(path, packageName);
        }
    }
}

void SettingsLoader::DisableDeletedProfiles()
{
    const auto& state = winrt::get_self<ApplicationState>(ApplicationState::SharedInstance());
    auto generatedProfileIds = state->GeneratedProfiles();
    bool newGeneratedProfiles = false;

    // See member description of userProfileCount.
    for (const auto& profile : gsl::make_span(userSettings.profiles).subspan(userProfileCount))
    {
        // Let's say a user doesn't know that they need to write `"hidden": true` in
        // order to prevent a profile from showing up (and a settings UI doesn't exist).
        // Naturally they would open settings.json and try to remove the profile object.
        // This section of code recognizes if a profile was seen before and marks it as
        // `"hidden": true` by default and thus ensures the behavior the user expects:
        // Profiles won't show up again after they've been removed from settings.json.
        if (generatedProfileIds.emplace(profile->Guid()).second)
        {
            newGeneratedProfiles = true;
        }
        else
        {
            profile->Deleted(true);
            profile->Hidden(true);
        }
    }

    if (newGeneratedProfiles)
    {
        state->GeneratedProfiles(generatedProfileIds);
    }
}

void SettingsLoader::FinalizeLayering()
{
    // Layer default globals -> user globals
    userSettings.globals->InsertParent(inboxSettings.globals);
    userSettings.globals->_FinalizeInheritance();
    // Layer default profile defaults -> user profile defaults
    userSettings.baseLayerProfile->InsertParent(inboxSettings.baseLayerProfile);
    userSettings.baseLayerProfile->_FinalizeInheritance();
    // Layer user profile defaults -> user profiles
    for (const auto& profile : userSettings.profiles)
    {
        profile->InsertParent(0, userSettings.baseLayerProfile);
        profile->_FinalizeInheritance();
    }
}

std::pair<size_t, size_t> SettingsLoader::_lineAndColumnFromPosition(const std::string_view& string, const size_t position)
{
    size_t line = 1;
    size_t pos = 0;

    for (;;)
    {
        const auto p = string.find('\n', pos);
        if (p >= position)
        {
            break;
        }

        pos = p + 1;
        line++;
    }

    return { line, position - pos + 1 };
}

void SettingsLoader::_rethrowSerializationExceptionWithLocationInfo(const JsonUtils::DeserializationError& e, std::string_view settingsString)
{
    std::string jsonValueAsString;
    try
    {
        jsonValueAsString = e.jsonValue.asString();
        if (e.jsonValue.isString())
        {
            jsonValueAsString = fmt::format("\"{}\"", jsonValueAsString);
        }
    }
    catch (...)
    {
        jsonValueAsString = "array or object";
    }

    const auto [line, column] = _lineAndColumnFromPosition(settingsString, static_cast<size_t>(e.jsonValue.getOffsetStart()));

    fmt::memory_buffer msg;
    fmt::format_to(msg, "* Line {}, Column {}", line, column);
    if (e.key)
    {
        fmt::format_to(msg, " ({})", *e.key);
    }
    fmt::format_to(msg, "\n  Have: {}\n  Expected: {}\0", jsonValueAsString, e.expectedType);

    throw SettingsTypedDeserializationException{ msg.data() };
}

Json::Value SettingsLoader::_parseJSON(const std::string_view& content)
{
    Json::Value json;
    std::string errs;
    const std::unique_ptr<Json::CharReader> reader{ Json::CharReaderBuilder::CharReaderBuilder().newCharReader() };

    if (!reader->parse(content.data(), content.data() + content.size(), &json, &errs))
    {
        throw winrt::hresult_error(WEB_E_INVALID_JSON_STRING, winrt::to_hstring(errs));
    }

    return json;
}

const Json::Value& SettingsLoader::_getJSONValue(const Json::Value& json, const std::string_view& key) noexcept
{
    if (json.isObject())
    {
        if (const auto val = json.find(key.data(), key.data() + key.size()))
        {
            return *val;
        }
    }

    return Json::Value::nullSingleton();
}

// We introduced a bug (GH#9962, fixed in GH#9964) that would result in one or
// more nameless, guid-less profiles being emitted into the user's settings file.
// Those profiles would show up in the list as "Default" later.
bool SettingsLoader::_isValidProfileObject(const Json::Value& profileJson)
{
    return profileJson.isObject() &&
           (profileJson.isMember(NameKey.data(), NameKey.data() + NameKey.size()) || // has a name (can generate a guid)
            profileJson.isMember(GuidKey.data(), GuidKey.data() + GuidKey.size())); // or has a guid
}

void SettingsLoader::_parse(const OriginTag origin, const std::string_view& content, ParsedSettings& settings)
{
    static constexpr std::string_view emptyObjectJSON{ "{}" };

    const auto json = _parseJSON(content.empty() ? emptyObjectJSON : content);
    const auto& profilesObject = _getJSONValue(json, ProfilesKey);
    const auto& defaultsObject = _getJSONValue(profilesObject, DefaultSettingsKey);
    const auto& profilesArray = profilesObject.isArray() ? profilesObject : _getJSONValue(profilesObject, ProfilesListKey);

    // globals
    {
        settings.globals = GlobalAppSettings::FromJson(json);

        if (const auto& schemes = _getJSONValue(json, SchemesKey))
        {
            for (const auto& schemeJson : schemes)
            {
                if (schemeJson.isObject() && ColorScheme::ValidateColorScheme(schemeJson))
                {
                    settings.globals->AddColorScheme(*ColorScheme::FromJson(schemeJson));
                }
            }
        }
    }

    // profiles.defaults
    {
        settings.baseLayerProfile = Profile::FromJson(defaultsObject);
        // Remove the `guid` member from the default settings.
        // That will hyper-explode, so just don't let them do that.
        settings.baseLayerProfile->ClearGuid();
        settings.baseLayerProfile->Origin(OriginTag::ProfilesDefaults);
    }

    // profiles.list
    {
        const auto size = profilesArray.size();

        settings.profiles.clear();
        settings.profiles.reserve(size);

        settings.profilesByGuid.clear();
        settings.profilesByGuid.reserve(size);

        for (const auto& profileJson : profilesArray)
        {
            if (_isValidProfileObject(profileJson))
            {
                auto profile = Profile::FromJson(profileJson);
                profile->Origin(origin);

                // Love it.
                if (!profile->HasGuid())
                {
                    profile->Guid(profile->Guid());
                }

                _appendProfile(std::move(profile), settings);
            }
        }
    }
}

void SettingsLoader::_appendProfile(winrt::com_ptr<Profile>&& profile, ParsedSettings& settings)
{
    // FYI: The static_cast ensures we don't move don't move the profile into
    // `profilesByGuid`, even though we still need it later for `profiles`.
    if (settings.profilesByGuid.emplace(profile->Guid(), static_cast<const winrt::com_ptr<Profile>&>(profile)).second)
    {
        settings.profiles.emplace_back(profile);
    }
    else
    {
        warnings.emplace_back(Model::SettingsLoadWarnings::DuplicateProfile);
    }
}

// Method Description:
// - Creates a CascadiaSettings from whatever's saved on disk, or instantiates
//      a new one with the default values. If we're running as a packaged app,
//      it will load the settings from our packaged localappdata. If we're
//      running as an unpackaged application, it will read it from the path
//      we've set under localappdata.
// - Loads both the settings from the defaults.json and the user's settings.json
// - Also runs and dynamic profile generators. If any of those generators create
//   new profiles, we'll write the user settings back to the file, with the new
//   profiles inserted into their list of profiles.
// Return Value:
// - a unique_ptr containing a new CascadiaSettings object.
Model::CascadiaSettings CascadiaSettings::LoadAll()
try
{
    const auto settingsString = ReadUTF8FileIfExists(_settingsPath()).value_or(std::string{});
    const auto firstTimeSetup = settingsString.empty();
    const auto settingsStringView = firstTimeSetup ? UserSettingsJson : settingsString;

    SettingsLoader loader{ DefaultJson, settingsStringView };

    // Generate dynamic profiles and add them as parents of user profiles.
    // That way the user profiles will get appropriate defaults from the generators (like icons and such).
    loader.GenerateProfiles();

    if (firstTimeSetup)
    {
        loader.FillBlanksInDefaultsJson();
    }

    loader.MergeInboxIntoUserProfiles();
    // Fragments might reference profiles generated by a generator.
    // --> MergeFragmentsIntoUserProfiles is called after MergeInboxIntoUserProfiles.
    loader.MergeFragmentsIntoUserProfiles();
    loader.DisableDeletedProfiles();
    loader.FinalizeLayering();

    // If this throws, the app will catch it and use the default settings.
    const auto settings = winrt::make_self<CascadiaSettings>(std::move(loader));

    // If we created the file, or found new dynamic profiles, write the user
    // settings string back to the file.
    if (firstTimeSetup)
    {
        try
        {
            settings->WriteSettingsToDisk();
        }
        catch (...)
        {
            LOG_CAUGHT_EXCEPTION();
            settings->_warnings.Append(SettingsLoadWarnings::FailedToWriteToSettings);
        }
    }

    return *settings;
}
catch (const SettingsException& ex)
{
    const auto settings{ winrt::make_self<CascadiaSettings>() };
    settings->_loadError = ex.Error();
    return *settings;
}
catch (const SettingsTypedDeserializationException& e)
{
    const auto settings{ winrt::make_self<CascadiaSettings>() };
    settings->_deserializationErrorMessage = til::u8u16(e.what());
    return *settings;
}

// Function Description:
// - Loads a batch of settings curated for the Universal variant of the terminal app
// Arguments:
// - <none>
// Return Value:
// - a unique_ptr to a CascadiaSettings with the connection types and settings for Universal terminal
Model::CascadiaSettings CascadiaSettings::LoadUniversal()
{
    return *winrt::make_self<CascadiaSettings>(std::string_view{}, DefaultUniversalJson);
}

// Function Description:
// - Creates a new CascadiaSettings object initialized with settings from the
//   hardcoded defaults.json.
// Arguments:
// - <none>
// Return Value:
// - a unique_ptr to a CascadiaSettings with the settings from defaults.json
Model::CascadiaSettings CascadiaSettings::LoadDefaults()
{
    return *winrt::make_self<CascadiaSettings>(std::string_view{}, DefaultJson);
}

CascadiaSettings::CascadiaSettings(const winrt::hstring& userJSON, const winrt::hstring& inboxJSON) :
    CascadiaSettings{ SettingsLoader::Default(til::u16u8(userJSON), til::u16u8(inboxJSON)) }
{
}

CascadiaSettings::CascadiaSettings(const std::string_view& userJSON, const std::string_view& inboxJSON) :
    CascadiaSettings{ SettingsLoader::Default(userJSON, inboxJSON) }
{
}

CascadiaSettings::CascadiaSettings(SettingsLoader&& loader)
{
    std::vector<Model::Profile> allProfiles;
    std::vector<Model::Profile> activeProfiles;

    allProfiles.reserve(loader.userSettings.profiles.size());
    activeProfiles.reserve(loader.userSettings.profiles.size());

    for (const auto& profile : loader.userSettings.profiles)
    {
        allProfiles.emplace_back(*profile);
        if (!profile->Hidden())
        {
            activeProfiles.emplace_back(*profile);
        }
    }

    if (allProfiles.empty())
    {
        throw SettingsException(SettingsLoadErrors::NoProfiles);
    }
    if (activeProfiles.empty())
    {
        throw SettingsException(SettingsLoadErrors::AllProfilesHidden);
    }

    // SettingsLoader and ParsedSettings are supposed
    // to always create these two members.
    assert(loader.userSettings.globals != nullptr);
    assert(loader.userSettings.baseLayerProfile != nullptr);

    _globals = loader.userSettings.globals;
    _baseLayerProfile = loader.userSettings.baseLayerProfile;
    _allProfiles = winrt::single_threaded_observable_vector(std::move(allProfiles));
    _activeProfiles = winrt::single_threaded_observable_vector(std::move(activeProfiles));
    _warnings = winrt::single_threaded_vector(std::move(loader.warnings));

    _finalizeSettings();
    _validateSettings();
}

// Method Description:
// - Returns the path of the settings.json file.
// Arguments:
// - <none>
// Return Value:
// - Returns a path in 80% of cases. I measured!
const std::filesystem::path& CascadiaSettings::_settingsPath()
{
    static const auto path = GetBaseSettingsPath() / SettingsFilename;
    return path;
}

// function Description:
// - Returns the full path to the settings file, either within the application
//   package, or in its unpackaged location. This path is under the "Local
//   AppData" folder, so it _doesn't_ roam to other machines.
// - If the application is unpackaged,
//   the file will end up under e.g. C:\Users\admin\AppData\Local\Microsoft\Windows Terminal\settings.json
// Arguments:
// - <none>
// Return Value:
// - the full path to the settings file
winrt::hstring CascadiaSettings::SettingsPath()
{
    return winrt::hstring{ _settingsPath().native() };
}

winrt::hstring CascadiaSettings::DefaultSettingsPath()
{
    // Both of these posts suggest getting the path to the exe, then removing
    // the exe's name to get the package root:
    // * https://blogs.msdn.microsoft.com/appconsult/2017/06/23/accessing-to-the-files-in-the-installation-folder-in-a-desktop-bridge-application/
    // * https://blogs.msdn.microsoft.com/appconsult/2017/03/06/handling-data-in-a-converted-desktop-app-with-the-desktop-bridge/
    //
    // This would break if we ever moved our exe out of the package root.
    // HOWEVER, if we try to look for a defaults.json that's simply in the same
    // directory as the exe, that will work for unpackaged scenarios as well. So
    // let's try that.

    std::wstring exePathString;
    THROW_IF_FAILED(wil::GetModuleFileNameW(nullptr, exePathString));

    std::filesystem::path path{ exePathString };
    path.replace_filename(DefaultsFilename);

    return winrt::hstring{ path.native() };
}

// Method Description:
// - Write the current state of CascadiaSettings to our settings file
// - Create a backup file with the current contents, if one does not exist
// - Persists the default terminal handler choice to the registry
// Arguments:
// - <none>
// Return Value:
// - <none>
void CascadiaSettings::WriteSettingsToDisk() const
{
    const auto settingsPath = _settingsPath();

    {
        // create a timestamped backup file
        const auto backupSettingsPath = fmt::format(L"{}.{:%Y-%m-%dT%H-%M-%S}.backup", settingsPath.native(), fmt::localtime(std::time(nullptr)));
        LOG_IF_WIN32_BOOL_FALSE(CopyFileW(settingsPath.c_str(), backupSettingsPath.c_str(), TRUE));
    }

    // write current settings to current settings file
    Json::StreamWriterBuilder wbuilder;
    wbuilder.settings_["indentation"] = "    ";
    wbuilder.settings_["enableYAMLCompatibility"] = true; // suppress spaces around colons

    const auto styledString{ Json::writeString(wbuilder, ToJson()) };
    WriteUTF8FileAtomic(settingsPath, styledString);

    // Persists the default terminal choice
    // GH#10003 - Only do this if _currentDefaultTerminal was actually initialized.
    if (_currentDefaultTerminal)
    {
        DefaultTerminal::Current(_currentDefaultTerminal);
    }
}

// Method Description:
// - Create a new serialized JsonObject from an instance of this class
// Arguments:
// - <none>
// Return Value:
// the JsonObject representing this instance
Json::Value CascadiaSettings::ToJson() const
{
    // top-level json object
    Json::Value json{ _globals->ToJson() };
    json["$help"] = "https://aka.ms/terminal-documentation";
    json["$schema"] = "https://aka.ms/terminal-profiles-schema";

    // "profiles" will always be serialized as an object
    Json::Value profiles{ Json::ValueType::objectValue };
    profiles[JsonKey(DefaultSettingsKey)] = _baseLayerProfile ? _baseLayerProfile->ToJson() : Json::ValueType::objectValue;
    Json::Value profilesList{ Json::ValueType::arrayValue };
    for (const auto& entry : _allProfiles)
    {
        if (!entry.Deleted())
        {
            const auto prof{ winrt::get_self<Profile>(entry) };
            profilesList.append(prof->ToJson());
        }
    }
    profiles[JsonKey(ProfilesListKey)] = profilesList;
    json[JsonKey(ProfilesKey)] = profiles;

    // TODO GH#8100:
    // "schemes" will be an accumulation of _all_ the color schemes
    // including all of the ones from defaults.json
    Json::Value schemes{ Json::ValueType::arrayValue };
    for (const auto& entry : _globals->ColorSchemes())
    {
        const auto scheme{ winrt::get_self<ColorScheme>(entry.Value()) };
        schemes.append(scheme->ToJson());
    }
    json[JsonKey(SchemesKey)] = schemes;

    return json;
}
