// First-run setup from the player's own XBLA package (see package_setup.cpp).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
bool package_extract(const std::filesystem::path& package, const std::filesystem::path& out, std::string& err, int* bad_hashes);
// Finds a package for title_id next to the exe and extracts it into assets.
// 1 = extracted, 0 = no package found, -1 = failed (err says why)
int package_setup_auto(const std::filesystem::path& exe_dir, const std::filesystem::path& assets, uint32_t title_id,
                       std::string& err, std::string& used);
// Runs on start-up: makes the assets folder from a package next to the exe if it isn't there yet.
// Returns false (after telling the player why) when the game can't start.
bool package_setup_first_run(const std::filesystem::path& exe_dir, const std::filesystem::path& assets, uint32_t title_id,
                             const std::string& game);
