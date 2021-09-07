// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "GlobalAppSettings.h"
#include "../../types/inc/Utils.hpp"
#include "JsonUtils.h"
#include "KeyChordSerialization.h"

#include "GlobalAppSettings.g.cpp"

using namespace Microsoft::Terminal::Settings::Model;
using namespace winrt::Microsoft::Terminal::Settings::Model::implementation;
using namespace winrt::Windows::UI::Xaml;
using namespace ::Microsoft::Console;
using namespace winrt::Microsoft::UI::Xaml::Controls;

static constexpr std::string_view LegacyKeybindingsKey{ "keybindings" };
static constexpr std::string_view ActionsKey{ "actions" };
static constexpr std::string_view DefaultProfileKey{ "defaultProfile" };
static constexpr std::string_view AlwaysShowTabsKey{ "alwaysShowTabs" };
static constexpr std::string_view InitialRowsKey{ "initialRows" };
static constexpr std::string_view InitialColsKey{ "initialCols" };
static constexpr std::string_view InitialPositionKey{ "initialPosition" };
static constexpr std::string_view CenterOnLaunchKey{ "centerOnLaunch" };
static constexpr std::string_view ShowTitleInTitlebarKey{ "showTerminalTitleInTitlebar" };
static constexpr std::string_view LanguageKey{ "language" };
static constexpr std::string_view ThemeKey{ "theme" };
static constexpr std::string_view TabWidthModeKey{ "tabWidthMode" };
static constexpr std::string_view UseAcrylicInTabRowKey{ "useAcrylicInTabRow" };
static constexpr std::string_view ShowTabsInTitlebarKey{ "showTabsInTitlebar" };
static constexpr std::string_view WordDelimitersKey{ "wordDelimiters" };
static constexpr std::string_view InputServiceWarningKey{ "inputServiceWarning" };
static constexpr std::string_view CopyOnSelectKey{ "copyOnSelect" };
static constexpr std::string_view CopyFormattingKey{ "copyFormatting" };
static constexpr std::string_view WarnAboutLargePasteKey{ "largePasteWarning" };
static constexpr std::string_view WarnAboutMultiLinePasteKey{ "multiLinePasteWarning" };
static constexpr std::string_view LaunchModeKey{ "launchMode" };
static constexpr std::string_view ConfirmCloseAllKey{ "confirmCloseAllTabs" };
static constexpr std::string_view SnapToGridOnResizeKey{ "snapToGridOnResize" };
static constexpr std::string_view EnableStartupTaskKey{ "startOnUserLogin" };
static constexpr std::string_view AlwaysOnTopKey{ "alwaysOnTop" };
static constexpr std::string_view LegacyUseTabSwitcherModeKey{ "useTabSwitcher" };
static constexpr std::string_view TabSwitcherModeKey{ "tabSwitcherMode" };
static constexpr std::string_view DisableAnimationsKey{ "disableAnimations" };
static constexpr std::string_view StartupActionsKey{ "startupActions" };
static constexpr std::string_view FocusFollowMouseKey{ "focusFollowMouse" };
static constexpr std::string_view WindowingBehaviorKey{ "windowingBehavior" };
static constexpr std::string_view TrimBlockSelectionKey{ "trimBlockSelection" };
static constexpr std::string_view AlwaysShowTrayIconKey{ "alwaysShowTrayIcon" };
static constexpr std::string_view MinimizeToTrayKey{ "minimizeToTray" };
static constexpr std::string_view DisabledProfileSourcesKey{ "disabledProfileSources" };

static constexpr std::string_view DebugFeaturesKey{ "debugFeatures" };

static constexpr std::string_view ForceFullRepaintRenderingKey{ "experimental.rendering.forceFullRepaint" };
static constexpr std::string_view SoftwareRenderingKey{ "experimental.rendering.software" };
static constexpr std::string_view ForceVTInputKey{ "experimental.input.forceVT" };
static constexpr std::string_view DetectURLsKey{ "experimental.detectURLs" };

// Method Description:
// - Copies any extraneous data from the parent before completing a CreateChild call
// Arguments:
// - <none>
// Return Value:
// - <none>
void GlobalAppSettings::_FinalizeInheritance()
{
    for (const auto& parent : _parents)
    {
        _actionMap->InsertParent(parent->_actionMap);
        _keybindingsWarnings.insert(_keybindingsWarnings.end(), parent->_keybindingsWarnings.begin(), parent->_keybindingsWarnings.end());
        for (const auto& [k, v] : parent->_colorSchemes)
        {
            _colorSchemes.Insert(k, v);
        }
    }
}

winrt::com_ptr<GlobalAppSettings> GlobalAppSettings::Copy() const
{
    auto globals{ winrt::make_self<GlobalAppSettings>() };
    globals->_InitialRows = _InitialRows;
    globals->_InitialCols = _InitialCols;
    globals->_AlwaysShowTabs = _AlwaysShowTabs;
    globals->_ShowTitleInTitlebar = _ShowTitleInTitlebar;
    globals->_ConfirmCloseAllTabs = _ConfirmCloseAllTabs;
    globals->_Language = _Language;
    globals->_Theme = _Theme;
    globals->_TabWidthMode = _TabWidthMode;
    globals->_UseAcrylicInTabRow = _UseAcrylicInTabRow;
    globals->_ShowTabsInTitlebar = _ShowTabsInTitlebar;
    globals->_WordDelimiters = _WordDelimiters;
    globals->_InputServiceWarning = _InputServiceWarning;
    globals->_CopyOnSelect = _CopyOnSelect;
    globals->_CopyFormatting = _CopyFormatting;
    globals->_WarnAboutLargePaste = _WarnAboutLargePaste;
    globals->_WarnAboutMultiLinePaste = _WarnAboutMultiLinePaste;
    globals->_InitialPosition = _InitialPosition;
    globals->_CenterOnLaunch = _CenterOnLaunch;
    globals->_LaunchMode = _LaunchMode;
    globals->_SnapToGridOnResize = _SnapToGridOnResize;
    globals->_ForceFullRepaintRendering = _ForceFullRepaintRendering;
    globals->_SoftwareRendering = _SoftwareRendering;
    globals->_ForceVTInput = _ForceVTInput;
    globals->_DebugFeaturesEnabled = _DebugFeaturesEnabled;
    globals->_StartOnUserLogin = _StartOnUserLogin;
    globals->_AlwaysOnTop = _AlwaysOnTop;
    globals->_TabSwitcherMode = _TabSwitcherMode;
    globals->_DisableAnimations = _DisableAnimations;
    globals->_StartupActions = _StartupActions;
    globals->_FocusFollowMouse = _FocusFollowMouse;
    globals->_WindowingBehavior = _WindowingBehavior;
    globals->_TrimBlockSelection = _TrimBlockSelection;
    globals->_DetectURLs = _DetectURLs;
    globals->_MinimizeToTray = _MinimizeToTray;
    globals->_AlwaysShowTrayIcon = _AlwaysShowTrayIcon;
    globals->_DisabledProfileSources = _DisabledProfileSources;
    globals->_UnparsedDefaultProfile = _UnparsedDefaultProfile;

    globals->_defaultProfile = _defaultProfile;
    globals->_actionMap = _actionMap->Copy();
    globals->_keybindingsWarnings = _keybindingsWarnings;

    if (_colorSchemes)
    {
        for (auto kv : _colorSchemes)
        {
            const auto schemeImpl{ winrt::get_self<ColorScheme>(kv.Value()) };
            globals->_colorSchemes.Insert(kv.Key(), *schemeImpl->Copy());
        }
    }

    for (const auto& parent : _parents)
    {
        globals->InsertParent(parent->Copy());
    }
    return globals;
}

winrt::Windows::Foundation::Collections::IMapView<winrt::hstring, winrt::Microsoft::Terminal::Settings::Model::ColorScheme> GlobalAppSettings::ColorSchemes() noexcept
{
    return _colorSchemes.GetView();
}

#pragma region DefaultProfile

void GlobalAppSettings::DefaultProfile(const winrt::guid& defaultProfile) noexcept
{
    _defaultProfile = defaultProfile;
    _UnparsedDefaultProfile = Utils::GuidToString(defaultProfile);
}

winrt::guid GlobalAppSettings::DefaultProfile() const
{
    return _defaultProfile;
}

#pragma endregion

winrt::Microsoft::Terminal::Settings::Model::ActionMap GlobalAppSettings::ActionMap() const noexcept
{
    return *_actionMap;
}

// Method Description:
// - Create a new instance of this class from a serialized JsonObject.
// Arguments:
// - json: an object which should be a serialization of a GlobalAppSettings object.
// Return Value:
// - a new GlobalAppSettings instance created from the values in `json`
winrt::com_ptr<GlobalAppSettings> GlobalAppSettings::FromJson(const Json::Value& json)
{
    auto result = winrt::make_self<GlobalAppSettings>();
    result->LayerJson(json);
    return result;
}

void GlobalAppSettings::LayerJson(const Json::Value& json)
{
    JsonUtils::GetValueForKey(json, DefaultProfileKey, _UnparsedDefaultProfile);
    JsonUtils::GetValueForKey(json, AlwaysShowTabsKey, _AlwaysShowTabs);
    JsonUtils::GetValueForKey(json, ConfirmCloseAllKey, _ConfirmCloseAllTabs);
    JsonUtils::GetValueForKey(json, InitialRowsKey, _InitialRows);
    JsonUtils::GetValueForKey(json, InitialColsKey, _InitialCols);
    JsonUtils::GetValueForKey(json, InitialPositionKey, _InitialPosition);
    JsonUtils::GetValueForKey(json, CenterOnLaunchKey, _CenterOnLaunch);
    JsonUtils::GetValueForKey(json, ShowTitleInTitlebarKey, _ShowTitleInTitlebar);
    JsonUtils::GetValueForKey(json, ShowTabsInTitlebarKey, _ShowTabsInTitlebar);
    JsonUtils::GetValueForKey(json, WordDelimitersKey, _WordDelimiters);
    JsonUtils::GetValueForKey(json, CopyOnSelectKey, _CopyOnSelect);
    JsonUtils::GetValueForKey(json, InputServiceWarningKey, _InputServiceWarning);
    JsonUtils::GetValueForKey(json, CopyFormattingKey, _CopyFormatting);
    JsonUtils::GetValueForKey(json, WarnAboutLargePasteKey, _WarnAboutLargePaste);
    JsonUtils::GetValueForKey(json, WarnAboutMultiLinePasteKey, _WarnAboutMultiLinePaste);
    JsonUtils::GetValueForKey(json, LaunchModeKey, _LaunchMode);
    JsonUtils::GetValueForKey(json, LanguageKey, _Language);
    JsonUtils::GetValueForKey(json, ThemeKey, _Theme);
    JsonUtils::GetValueForKey(json, TabWidthModeKey, _TabWidthMode);
    JsonUtils::GetValueForKey(json, UseAcrylicInTabRowKey, _UseAcrylicInTabRow);
    JsonUtils::GetValueForKey(json, SnapToGridOnResizeKey, _SnapToGridOnResize);

    // GetValueForKey will only override the current value if the key exists
    JsonUtils::GetValueForKey(json, DebugFeaturesKey, _DebugFeaturesEnabled);
    JsonUtils::GetValueForKey(json, ForceFullRepaintRenderingKey, _ForceFullRepaintRendering);
    JsonUtils::GetValueForKey(json, SoftwareRenderingKey, _SoftwareRendering);
    JsonUtils::GetValueForKey(json, ForceVTInputKey, _ForceVTInput);
    JsonUtils::GetValueForKey(json, EnableStartupTaskKey, _StartOnUserLogin);
    JsonUtils::GetValueForKey(json, AlwaysOnTopKey, _AlwaysOnTop);

    // GH#8076 - when adding enum values to this key, we also changed it from
    // "useTabSwitcher" to "tabSwitcherMode". Continue supporting
    // "useTabSwitcher", but prefer "tabSwitcherMode"
    JsonUtils::GetValueForKey(json, LegacyUseTabSwitcherModeKey, _TabSwitcherMode);
    JsonUtils::GetValueForKey(json, TabSwitcherModeKey, _TabSwitcherMode);
    JsonUtils::GetValueForKey(json, DisableAnimationsKey, _DisableAnimations);
    JsonUtils::GetValueForKey(json, StartupActionsKey, _StartupActions);
    JsonUtils::GetValueForKey(json, FocusFollowMouseKey, _FocusFollowMouse);
    JsonUtils::GetValueForKey(json, WindowingBehaviorKey, _WindowingBehavior);
    JsonUtils::GetValueForKey(json, TrimBlockSelectionKey, _TrimBlockSelection);
    JsonUtils::GetValueForKey(json, DetectURLsKey, _DetectURLs);
    JsonUtils::GetValueForKey(json, MinimizeToTrayKey, _MinimizeToTray);
    JsonUtils::GetValueForKey(json, AlwaysShowTrayIconKey, _AlwaysShowTrayIcon);
    JsonUtils::GetValueForKey(json, DisabledProfileSourcesKey, _DisabledProfileSources);

    // This is a helper lambda to get the keybindings and commands out of both
    // and array of objects. We'll use this twice, once on the legacy
    // `keybindings` key, and again on the newer `bindings` key.
    auto parseBindings = [this, &json](auto jsonKey) {
        if (auto bindings{ json[JsonKey(jsonKey)] })
        {
            auto warnings = _actionMap->LayerJson(bindings);

            // It's possible that the user provided keybindings have some warnings
            // in them - problems that we should alert the user to, but we can
            // recover from. Most of these warnings cannot be detected later in the
            // Validate settings phase, so we'll collect them now. If there were any
            // warnings generated from parsing these keybindings, add them to our
            // list of warnings.
            _keybindingsWarnings.insert(_keybindingsWarnings.end(), warnings.begin(), warnings.end());
        }
    };
    parseBindings(LegacyKeybindingsKey);
    parseBindings(ActionsKey);
}

// Method Description:
// - Adds the given colorscheme to our map of schemes, using its name as the key.
// Arguments:
// - scheme: the color scheme to add
// Return Value:
// - <none>
void GlobalAppSettings::AddColorScheme(const Model::ColorScheme& scheme)
{
    _colorSchemes.Insert(scheme.Name(), scheme);
}

void GlobalAppSettings::RemoveColorScheme(hstring schemeName)
{
    _colorSchemes.TryRemove(schemeName);
}

// Method Description:
// - Return the warnings that we've collected during parsing the JSON for the
//   keybindings. It's possible that the user provided keybindings have some
//   warnings in them - problems that we should alert the user to, but we can
//   recover from.
// Arguments:
// - <none>
// Return Value:
// - <none>
const std::vector<winrt::Microsoft::Terminal::Settings::Model::SettingsLoadWarnings>& GlobalAppSettings::KeybindingsWarnings() const
{
    return _keybindingsWarnings;
}

// Method Description:
// - Create a new serialized JsonObject from an instance of this class
// Arguments:
// - <none>
// Return Value:
// - the JsonObject representing this instance
Json::Value GlobalAppSettings::ToJson() const
{
    Json::Value json{ Json::ValueType::objectValue };

    // clang-format off
    JsonUtils::SetValueForKey(json, DefaultProfileKey,              _UnparsedDefaultProfile);
    JsonUtils::SetValueForKey(json, AlwaysShowTabsKey,              _AlwaysShowTabs);
    JsonUtils::SetValueForKey(json, ConfirmCloseAllKey,             _ConfirmCloseAllTabs);
    JsonUtils::SetValueForKey(json, InitialRowsKey,                 _InitialRows);
    JsonUtils::SetValueForKey(json, InitialColsKey,                 _InitialCols);
    JsonUtils::SetValueForKey(json, InitialPositionKey,             _InitialPosition);
    JsonUtils::SetValueForKey(json, CenterOnLaunchKey,              _CenterOnLaunch);
    JsonUtils::SetValueForKey(json, ShowTitleInTitlebarKey,         _ShowTitleInTitlebar);
    JsonUtils::SetValueForKey(json, ShowTabsInTitlebarKey,          _ShowTabsInTitlebar);
    JsonUtils::SetValueForKey(json, WordDelimitersKey,              _WordDelimiters);
    JsonUtils::SetValueForKey(json, InputServiceWarningKey,         _InputServiceWarning);
    JsonUtils::SetValueForKey(json, CopyOnSelectKey,                _CopyOnSelect);
    JsonUtils::SetValueForKey(json, CopyFormattingKey,              _CopyFormatting);
    JsonUtils::SetValueForKey(json, WarnAboutLargePasteKey,         _WarnAboutLargePaste);
    JsonUtils::SetValueForKey(json, WarnAboutMultiLinePasteKey,     _WarnAboutMultiLinePaste);
    JsonUtils::SetValueForKey(json, LaunchModeKey,                  _LaunchMode);
    JsonUtils::SetValueForKey(json, LanguageKey,                    _Language);
    JsonUtils::SetValueForKey(json, ThemeKey,                       _Theme);
    JsonUtils::SetValueForKey(json, TabWidthModeKey,                _TabWidthMode);
    JsonUtils::SetValueForKey(json, UseAcrylicInTabRowKey,          _UseAcrylicInTabRow);
    JsonUtils::SetValueForKey(json, SnapToGridOnResizeKey,          _SnapToGridOnResize);
    JsonUtils::SetValueForKey(json, DebugFeaturesKey,               _DebugFeaturesEnabled);
    JsonUtils::SetValueForKey(json, ForceFullRepaintRenderingKey,   _ForceFullRepaintRendering);
    JsonUtils::SetValueForKey(json, SoftwareRenderingKey,           _SoftwareRendering);
    JsonUtils::SetValueForKey(json, ForceVTInputKey,                _ForceVTInput);
    JsonUtils::SetValueForKey(json, EnableStartupTaskKey,           _StartOnUserLogin);
    JsonUtils::SetValueForKey(json, AlwaysOnTopKey,                 _AlwaysOnTop);
    JsonUtils::SetValueForKey(json, TabSwitcherModeKey,             _TabSwitcherMode);
    JsonUtils::SetValueForKey(json, DisableAnimationsKey,           _DisableAnimations);
    JsonUtils::SetValueForKey(json, StartupActionsKey,              _StartupActions);
    JsonUtils::SetValueForKey(json, FocusFollowMouseKey,            _FocusFollowMouse);
    JsonUtils::SetValueForKey(json, WindowingBehaviorKey,           _WindowingBehavior);
    JsonUtils::SetValueForKey(json, TrimBlockSelectionKey,          _TrimBlockSelection);
    JsonUtils::SetValueForKey(json, DetectURLsKey,                  _DetectURLs);
    JsonUtils::SetValueForKey(json, MinimizeToTrayKey,              _MinimizeToTray);
    JsonUtils::SetValueForKey(json, AlwaysShowTrayIconKey,          _AlwaysShowTrayIcon);
    JsonUtils::SetValueForKey(json, DisabledProfileSourcesKey,      _DisabledProfileSources);
    // clang-format on

    json[JsonKey(ActionsKey)] = _actionMap->ToJson();
    return json;
}
