/*++
Copyright (c) Microsoft Corporation
Licensed under the MIT license.

Module Name:
- CascadiaSettings.h

Abstract:
- This class acts as the container for all app settings. It's composed of two
        parts: Globals, which are app-wide settings, and Profiles, which contain
        a set of settings that apply to a single instance of the terminal.
  Also contains the logic for serializing and deserializing this object.

Author(s):
- Mike Griese - March 2019

--*/
#pragma once

#include "CascadiaSettings.g.h"

#include "GlobalAppSettings.h"

#include "Profile.h"
#include "ColorScheme.h"

namespace winrt::Microsoft::Terminal::Settings::Model::implementation
{
    winrt::com_ptr<Profile> ReproduceProfile(const winrt::com_ptr<Profile>& parent);

    class SettingsTypedDeserializationException final : public std::runtime_error
    {
    public:
        SettingsTypedDeserializationException(const char* message) noexcept :
            std::runtime_error(message) {}
    };

    struct ParsedSettings
    {
        winrt::com_ptr<implementation::GlobalAppSettings> globals;
        winrt::com_ptr<implementation::Profile> baseLayerProfile;
        std::vector<winrt::com_ptr<implementation::Profile>> profiles;
        std::unordered_map<winrt::guid, winrt::com_ptr<implementation::Profile>> profilesByGuid;
    };

    struct SettingsLoader
    {
        static SettingsLoader Default(const std::string_view& userJSON, const std::string_view& inboxJSON);
        SettingsLoader(const std::string_view& userJSON, const std::string_view& inboxJSON);

        void GenerateProfiles();
        void FillBlanksInDefaultsJson();
        void MergeInboxIntoUserProfiles();
        void MergeFragmentsIntoUserProfiles();
        void DisableDeletedProfiles();
        void FinalizeLayering();

        ParsedSettings inboxSettings;
        ParsedSettings userSettings;
        std::vector<Model::SettingsLoadWarnings> warnings;

    private:
        static std::pair<size_t, size_t> _lineAndColumnFromPosition(const std::string_view& string, const size_t position);
        static void _rethrowSerializationExceptionWithLocationInfo(const JsonUtils::DeserializationError& e, std::string_view settingsString);
        static Json::Value _parseJSON(const std::string_view& content);
        static const Json::Value& _getJSONValue(const Json::Value& json, const std::string_view& key) noexcept;
        static bool _isValidProfileObject(const Json::Value& profileJson);
        void _parse(const OriginTag origin, const std::string_view& content, ParsedSettings& settings);
        void _appendProfile(winrt::com_ptr<implementation::Profile>&& profile, ParsedSettings& settings);

        std::unordered_set<std::wstring_view> ignoredNamespaces;
        // We treat userSettings.profiles as an append-only array and will
        // append profiles into the userSettings as necessary in this function.
        // We can thus get the gsl::span of user-given profiles, by preserving the size here
        // and restoring it with gsl::make_span(userSettings.profiles).subspan(userProfileCount).
        size_t userProfileCount = 0;
    };

    struct CascadiaSettings : CascadiaSettingsT<CascadiaSettings>
    {
    public:
        static Model::CascadiaSettings LoadDefaults();
        static Model::CascadiaSettings LoadAll();
        static Model::CascadiaSettings LoadUniversal();

        static winrt::hstring SettingsPath();
        static winrt::hstring DefaultSettingsPath();
        static winrt::hstring ApplicationDisplayName();
        static winrt::hstring ApplicationVersion();

        CascadiaSettings() noexcept = default;
        CascadiaSettings(const winrt::hstring& userJSON, const winrt::hstring& inboxJSON);
        CascadiaSettings(const std::string_view& userJSON, const std::string_view& inboxJSON = {});
        explicit CascadiaSettings(SettingsLoader&& loader);

        // user settings
        Model::CascadiaSettings Copy() const;
        Model::GlobalAppSettings GlobalSettings() const;
        winrt::Windows::Foundation::Collections::IObservableVector<Model::Profile> AllProfiles() const noexcept;
        winrt::Windows::Foundation::Collections::IObservableVector<Model::Profile> ActiveProfiles() const noexcept;
        Model::ActionMap ActionMap() const noexcept;
        void WriteSettingsToDisk() const;
        Json::Value ToJson() const;
        Model::Profile ProfileDefaults() const;
        Model::Profile CreateNewProfile();
        Model::Profile FindProfile(const winrt::guid& guid) const noexcept;
        Model::ColorScheme GetColorSchemeForProfile(const Model::Profile& profile) const;
        void UpdateColorSchemeReferences(const winrt::hstring& oldName, const winrt::hstring& newName);
        Model::Profile GetProfileForArgs(const Model::NewTerminalArgs& newTerminalArgs) const;
        Model::Profile GetProfileByName(const winrt::hstring& name) const;
        Model::Profile GetProfileByIndex(uint32_t index) const;
        Model::Profile DuplicateProfile(const Model::Profile& source);

        // load errors
        winrt::Windows::Foundation::Collections::IVectorView<Model::SettingsLoadWarnings> Warnings() const;
        winrt::Windows::Foundation::IReference<Model::SettingsLoadErrors> GetLoadingError() const;
        winrt::hstring GetSerializationErrorMessage() const;

        // defterm
        static bool IsDefaultTerminalAvailable() noexcept;
        winrt::Windows::Foundation::Collections::IObservableVector<Model::DefaultTerminal> DefaultTerminals() const noexcept;
        Model::DefaultTerminal CurrentDefaultTerminal() noexcept;
        void CurrentDefaultTerminal(const Model::DefaultTerminal& terminal);

    private:
        static const std::filesystem::path& _settingsPath();

        winrt::com_ptr<implementation::Profile> _createNewProfile(const std::wstring_view& name) const;

        void _finalizeSettings() const;

        void _validateSettings();
        void _validateAllSchemesExist();
        void _validateMediaResources();
        void _validateKeybindings() const;
        void _validateColorSchemesInCommands() const;
        bool _hasInvalidColorScheme(const Model::Command& command) const;

        // user settings
        winrt::com_ptr<implementation::GlobalAppSettings> _globals;
        winrt::com_ptr<implementation::Profile> _baseLayerProfile;
        winrt::Windows::Foundation::Collections::IObservableVector<Model::Profile> _allProfiles;
        winrt::Windows::Foundation::Collections::IObservableVector<Model::Profile> _activeProfiles;

        // load errors
        winrt::Windows::Foundation::Collections::IVector<Model::SettingsLoadWarnings> _warnings;
        winrt::Windows::Foundation::IReference<Model::SettingsLoadErrors> _loadError;
        winrt::hstring _deserializationErrorMessage;

        // defterm
        Model::DefaultTerminal _currentDefaultTerminal{ nullptr };
    };
}

namespace winrt::Microsoft::Terminal::Settings::Model::factory_implementation
{
    BASIC_FACTORY(CascadiaSettings);
}
