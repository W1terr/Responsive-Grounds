#pragma once

// Texts in the languages Skyrim ships with. English is built in (the English texts are the keys), the others come from
// Data\SKSE\Plugins\ResponsiveGrounds\lang\<language>.json. "auto" follows the game's sLanguage.
// T() returns pointers into the loaded table: switch languages only where the texts are drawn (the menu thread).
namespace Lang
{
	struct Language
	{
		const char* id;      // file name, same names as Skyrim's sLanguage
		const char* name;    // shown in the language list
		const char* glyphs;  // SKSE Menu Framework "Character Glyphs" option the language needs, nullptr if the default font has its letters
	};

	std::span<const Language> All();
	const Language&           Current();

	// "auto" = the game's language; unknown ids fall back to English
	void Set(std::string_view a_id);

	// translation of an English text, the text itself if there is none
	const char* T(const char* a_english);

	// T() of a std::format string, formatted with the arguments
	template <class... Args>
	std::string F(const char* a_english, const Args&... a_args)
	{
		try {
			return std::vformat(T(a_english), std::make_format_args(a_args...));
		} catch (const std::format_error&) {
			return std::vformat(a_english, std::make_format_args(a_args...));  // broken translation
		}
	}
}
