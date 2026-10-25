/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe::Mac {

// The files the drag pasteboard (else the general one) offers when its
// public.url is `link`. Asking makes Firefox write the dragged picture.
[[nodiscard]] QList<QUrl> BrowserFiles(const QUrl &link);

} // namespace Safe::Mac
