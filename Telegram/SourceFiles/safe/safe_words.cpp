/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_words.h"

#include "core/utils.h"

namespace Safe {
namespace {

constexpr auto kMaxQuery = 256;

// Long, unambiguous stems that count anywhere, even glued ("hotporn").
constexpr auto kAnywhere = std::array{
	"porn", "hentai", "nsfw", "xxx", "blowjob", "handjob", "rimjob",
	"footjob", "onlyfans", "onlyfan", "pornhub", "xvideos", "xhamster",
	"redtube", "youporn", "brazzers", "chaturbate", "dildo", "orgasm",
	"masturbat", "fuck", "phuck", "cumshot", "creampie", "gangbang",
	"bukkake", "deepthroat", "camgirl", "camwhore", "sexting", "nudity",
	"nudes", "boobs", "boobies", "titties", "tiddies", "pussy", "pussies",
	"vagina", "penis", "clitoris", "dickpic", "threesome", "foursome",
	"bdsm", "ahegao", "erotic", "striptease", "stripper", "lingerie",
	"upskirt", "topless", "seggs", "nympho", "whore", "slutty", "sluts",
	"hooker", "jizz", "fetish", "kinky", "erection", "testicle", "scrotum",
	"nipple", "areola", "seks", "golaya", "golye", "siski", "pizda",
};

// A word that starts with one of these counts ("sexy", "nudes").
constexpr auto kPrefix = std::array{
	"sex", "nude", "nudist", "naked", "nekkid", "nakey", "boob", "booty",
	"breast", "kink", "lewd", "ecchi", "orgy", "orgie", "bondage", "slut",
	"cunt", "twat", "bitch", "thot", "bussy", "cameltoe", "clit", "vulva",
	"anus", "semen", "sperm", "seduc", "panties", "pantie", "undress",
	"nsfl", "playboy", "escort", "hoes", "hoez", "twerk", "thicc", "titty",
	"tittie", "tities", "naughty", "spank", "strapon", "bikini", "thong",
	"nudez", "pussi", "pusy", "fck", "smut", "wank", "spunk", "horny",
	"hornie", "boner", "labia",
};

// Whole words only, so "ass" never catches "class" or "assistant".
constexpr auto kWord = std::array{
	"ass", "asses", "asshole", "assholes", "azz", "arse", "arses", "butt",
	"butts", "butthole", "tit", "tits", "tity", "cum", "cums", "cumming",
	"coom", "dick", "dicks", "dik", "diks", "dck", "cock", "cocks", "cok",
	"anal", "bra", "bras", "hoe", "lust", "lusty", "lustful", "fap",
	"fapping", "jerkoff", "bj", "gspot", "69", "strip", "stripping", "puss",
	"nood", "noods", "nudie", "nudies", "shlong", "schlong", "peen",
	"peepee", "ballsack", "humping", "booba", "bewbs", "milf", "milfs",
	"dilf", "pron", "fuk", "fukk", "fuks", "fuq", "phuk",
};

constexpr auto kEmoji = std::array{
	u"\U0001F346", // aubergine
	u"\U0001F351", // peach
	u"\U0001F4A6", // sweat droplets
	u"\U0001F445", // tongue
	u"\U0001F51E", // no one under eighteen
	u"\U0001FAE6", // biting lip
	u"\U0001F975", // hot face
	u"\U0001F459", // bikini
	u"\U0001FA72", // briefs
	u"\U0001F34C\U0001F4A6", // banana + droplets
};

[[nodiscard]] QChar Fold(QChar c) {
	switch (c.unicode()) {
	case '0': return u'o';
	case '1':
	case '!':
	case '|': return u'i';
	case '3': return u'e';
	case '4':
	case '@': return u'a';
	case '5':
	case '$': return u's';
	case '7': return u't';
	case '8': return u'b';
	}
	return c;
}

[[nodiscard]] QStringList Words(const QString &text, bool foldLeet) {
	const auto decomposed = text.normalized(
		QString::NormalizationForm_D).toLower();
	auto result = QStringList();
	auto word = QString();
	for (const auto c : decomposed) {
		if (c.category() == QChar::Mark_NonSpacing) {
			continue;
		}
		const auto folded = foldLeet ? Fold(c) : c;
		const auto u = folded.unicode();
		if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) {
			word.append(folded);
		} else if (!word.isEmpty()) {
			result.push_back(base::take(word));
		}
	}
	if (!word.isEmpty()) {
		result.push_back(word);
	}
	return result;
}

[[nodiscard]] bool MatchesWord(const QString &word) {
	if (word.isEmpty()) {
		return false;
	}
	const auto contains = [&](const char *stem) {
		return word.contains(QLatin1String(stem));
	};
	const auto starts = [&](const char *stem) {
		return word.startsWith(QLatin1String(stem));
	};
	const auto equals = [&](const char *entry) {
		return (word == QLatin1String(entry));
	};
	return ranges::any_of(kAnywhere, contains)
		|| ranges::any_of(kPrefix, starts)
		|| ranges::any_of(kWord, equals);
}

[[nodiscard]] bool MatchesWords(const QStringList &words) {
	if (ranges::any_of(words, MatchesWord)) {
		return true;
	}
	return (words.size() > 1) && MatchesWord(words.join(QString()));
}

[[nodiscard]] bool Matches(const QString &text) {
	return MatchesWords(Words(text, true))
		|| MatchesWords(Words(text, false));
}

} // namespace

bool IsGuardedQuery(const QString &query) {
	if (query.isEmpty()) {
		return false;
	}
	const auto raw = query.left(kMaxQuery);
	const auto compact = QString(raw).remove(QChar(0xFE0F));
	const auto hasEmoji = [&](const char16_t *emoji) {
		return compact.contains(QString::fromUtf16(emoji));
	};
	if (ranges::any_of(kEmoji, hasEmoji) || Matches(raw)) {
		return true;
	}
	const auto translit = translitRusEng(raw);
	return (translit != raw) && Matches(translit.left(kMaxQuery));
}

} // namespace Safe
