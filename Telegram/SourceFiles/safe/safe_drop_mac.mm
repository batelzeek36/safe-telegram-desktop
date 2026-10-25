/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_drop_mac.h"

#include "base/platform/mac/base_utilities_mac.h"

#include <Cocoa/Cocoa.h>

namespace Safe::Mac {
namespace {

using Platform::NS2QString;

struct Remembered {
	QString board;
	NSInteger changeCount = 0;
	QUrl link;
	QList<QUrl> files;
};

[[nodiscard]] bool SameLink(NSString *carried, const QUrl &link) {
	if (!carried) {
		return false;
	}
	const auto url = QUrl(NS2QString(carried));
	const auto options = QUrl::NormalizePathSegments
		| QUrl::StripTrailingSlash;
	return url.isValid() && (url.adjusted(options) == link.adjusted(options));
}

[[nodiscard]] QList<QUrl> FilesOf(NSPasteboard *board) {
	auto result = QList<QUrl>();
	for (NSPasteboardItem *item in [board pasteboardItems]) {
		NSString *value = [item stringForType:NSPasteboardTypeFileURL];
		NSURL *url = value ? [NSURL URLWithString:value] : nil;
		if (!url || ![url isFileURL]) {
			continue;
		}
		const auto path = NS2QString([[url URLByStandardizingPath] path]);
		if (!path.isEmpty() && QFileInfo(path).isFile()) {
			result.push_back(QUrl::fromLocalFile(path));
		}
	}
	return result;
}

} // namespace

QList<QUrl> BrowserFiles(const QUrl &link) {
	static auto remembered = Remembered();
	@autoreleasepool {
		for (NSPasteboardName name in @[
				NSPasteboardNameDrag,
				NSPasteboardNameGeneral]) {
			NSPasteboard *board = [NSPasteboard pasteboardWithName:name];
			const auto boardName = NS2QString(name);
			const auto changeCount = [board changeCount];
			if (remembered.board == boardName
				&& remembered.changeCount == changeCount
				&& remembered.link == link) {
				return remembered.files;
			}
			NSArray<NSPasteboardType> *types = [board types];
			if (![types containsObject:NSPasteboardTypeFileURL]
				|| !SameLink([board stringForType:NSPasteboardTypeURL], link)) {
				continue;
			}
			remembered = Remembered{
				.board = boardName,
				.changeCount = changeCount,
				.link = link,
				.files = FilesOf(board),
			};
			return remembered.files;
		}
	}
	return {};
}

} // namespace Safe::Mac
