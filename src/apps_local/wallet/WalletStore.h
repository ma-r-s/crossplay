#pragma once

// The card files. Device-only: SD I/O and nothing else, so reading it is the
// review. What the files MEAN lives in WalletCore, which has a host suite.
//
// /cards sits beside /notes and /workouts rather than under /.crosspoint, so a
// card can be read, fixed or copied to another reader on a computer.

#include <string>
#include <vector>

#include "WalletCore.h"

namespace wallet {
namespace store {

constexpr const char* kDir = "/cards";

// Creates /cards on first use. False only when the SD card refuses.
bool begin();

// Every readable card, in the order they were added. A file that is not a card
// is skipped rather than refusing the rest.
std::vector<Card> loadAll();

// One card by its file name; false when it is gone or no longer a card.
bool load(const std::string& file, Card& out);

// How many card files there are, without reading them.
size_t count();

// Saves a new card under the next free name, which it writes into card.file.
bool add(Card& card);

bool remove(const std::string& file);

}  // namespace store
}  // namespace wallet
