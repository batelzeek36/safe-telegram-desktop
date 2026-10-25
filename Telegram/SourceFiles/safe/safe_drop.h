/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QMimeData;

namespace Safe {

// Pictures dragged or pasted in from a web browser (d21-gif-drop, Zach
// 2026-10-25: "they drag as images, not GIFs").
//
// A browser drag on macOS carries the page's link (public.url), a still of
// the picture's current frame (TIFF and PNG) and, when asked, the picture
// file itself (public.file-url: Firefox writes it at that moment). Qt reads
// text/uri-list through its newest converter first, which is the link, so
// Telegram Desktop found no local file and sent the still: a GIF went out as
// a photo. The native Mac app asks for the file, which is why it worked.

// The browser's own files instead of `urls` when every url is remote
// (http / https), the data also carries a picture, and the pasteboard that
// carries that link offers files (macOS; elsewhere `urls` comes back
// unchanged). Asked once per drag or copy, then remembered.
[[nodiscard]] QList<QUrl> PreferBrowserFiles(
	not_null<const QMimeData*> data,
	QList<QUrl> urls);

// A picture from Tenor (its media link in the urls, the page's <img> or the
// text): Tenor's own MP4 of it is fetched and `send` gets that local file,
// which goes out as a GIF (soundless H.264 is Telegram's GIF format). Tenor's
// grid gives Firefox a small GIF or an animated WebP, which Telegram would
// send small or as a still. If the fetch fails, `send` gets the browser's
// file when there was one, else a toast says so. True when it took the drop.
bool TakeHostedGif(
	not_null<const QMimeData*> data,
	Fn<void(QStringList)> send);

} // namespace Safe
