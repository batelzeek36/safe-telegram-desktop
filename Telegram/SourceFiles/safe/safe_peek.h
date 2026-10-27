/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class ClickHandler;
struct TextWithEntities;

namespace Ui {
class GenericBox;
} // namespace Ui

// A private peek at what was held back in the quiet chats (d32). Zach
// 2026-10-26: "id like to be able to tap something to see what jade or my
// mom said or what gif they sent", picked "Private peek" and "family only".
//
// In the quiet chats (safe_quiet_media.h: the family group and his private
// chats with Michelle and with Jade) the italic kind word that stands in for
// a picture ("GIF", "3 photos") and the small leaf by the time of a message
// Calm softened (safe_calm.h, their side) can be tapped. That opens a box over
// the dimmed chat, the same box look as "What you typed": the picture, GIF or
// sticker as it is, and the words she wrote before they were softened. The
// chat itself stays quiet; closing the box leaves everything as it was.
// Nothing is sent, marked or changed on the account.
namespace Safe::Peek {

// A message there with something held back that the box can show.
[[nodiscard]] bool Peekable(not_null<HistoryItem*> item);

// The tap: opens the box in the window it was tapped in. One handler per
// message, so a press and its release meet the same link.
[[nodiscard]] std::shared_ptr<ClickHandler> Link(
	not_null<HistoryItem*> item);

void PeekBox(not_null<Ui::GenericBox*> box, not_null<HistoryItem*> item);

} // namespace Safe::Peek
