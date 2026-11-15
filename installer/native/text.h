// Nebula Setup's words in the player's language, from localization/NebulaText.tsv (embedded as the NEBULATEXT
// resource; the game has the same table). A row is found by its English cell. The language is runtime_settings.ini's
// `language` key, for "auto" or none the system's. An empty cell uses the language's other spelling (EuFrench for
// UsFrench and so on); no table, row or cell: the English given.
#pragma once

#include "common.h"

#include <initializer_list>

namespace nebula::text {

// `english` in the player's language.
wstring Of(const wchar_t* english);
// The same with {0}, {1}, ... replaced by `values` in order.
wstring Of(const wchar_t* english, std::initializer_list<wstring> values);

}  // namespace nebula::text
