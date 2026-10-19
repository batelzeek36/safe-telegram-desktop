# This file is part of Safe Telegram, a locked build of Telegram Desktop.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Own bundle id and app name, so the Mac never mixes Safe Telegram with an
# official Telegram Desktop (settings, Keychain items, notifications, Dock).
if (APPLE AND NOT build_macstore)
    set(bundle_identifier "com.zakkbates.safetelegram.desktop")
    set(output_name "Safe Telegram")
endif()
