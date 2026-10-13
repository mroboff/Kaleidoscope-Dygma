/* -*- mode: c++ -*-
 * ChordsDygma -- Combos: several keys pressed together do something else
 * Copyright (C) 2026  Mark Roboff (Keyboard Curator firmware fork)
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <Kaleidoscope.h>
#include <cstdint>

namespace kaleidoscope {
namespace plugin {

/*
 * Chords (combos) stored in EEPROM: when every key of a chord is pressed
 * within its timeout, the chord's result key is pressed in their place,
 * and released when the first of them is released.
 *
 * Presses of keys that belong to a chord on the current layer are held
 * back until the chord completes, a key outside every candidate chord is
 * pressed, one of the held keys is released, or the timeout passes; then
 * they are let through in order, as ordinary presses.
 *
 * Focus commands:
 *   chords.map      get/set the map. Each chord is
 *                   `result layers timeout key1 ... keyN 0`: the result
 *                   key code (as in keymap.custom), a bitmask of the layers
 *                   it applies to (0 for every layer), its own timeout in
 *                   ms (0 for chords.timeout), then its keys as keymap
 *                   positions + 1 (1..80), closed with a 0. The map ends
 *                   with `0 0`, like superkeys.map. The keyboard sends an
 *                   erased map as `0 0`.
 *   chords.timeout  get/set the timeout used by chords whose own is 0, ms.
 *   chords.memory   the map's room, in bytes.
 */
class Chords : public kaleidoscope::Plugin {
 public:
  static constexpr uint16_t MAP_WORDS = 256;
  static constexpr uint8_t MAX_CHORDS = 32;
  static constexpr uint8_t MAX_KEYS = 6;
  static constexpr uint8_t MAX_ACTIVE = 4;
  static constexpr uint16_t DEFAULT_TIMEOUT_MS = 50;

  struct Chord {
    uint16_t result;
    uint16_t layers;   // bit n: layer n; 0: every layer
    uint16_t timeout;  // 0: the keyboard's chords.timeout
    uint8_t count;
    uint8_t keys[MAX_KEYS];  // keymap positions
  };

  Chords() {}

  EventHandlerResult onKeyswitchEvent(Key &mapped_key, KeyAddr key_addr, uint8_t key_state);
  EventHandlerResult beforeReportingState();
  EventHandlerResult onFocusEvent(const char *command);

  // Reserves the EEPROM slice and loads the map. Called from the sketch
  // after every stock plugin has reserved its storage.
  static void reserve_storage();

 private:
  struct Pending {
    KeyAddr addr;
    Key key;
  };
  struct Active {
    uint8_t chord;
    uint8_t held;  // bit n: key n of the chord is still down
    KeyAddr addr;  // the address the result is pressed at
  };

  static uint16_t storage_base_;
  static bool enabled_;
  static uint16_t timeout_ms_;
  static Chord chords_[MAX_CHORDS];
  static uint8_t count_;

  Pending queue_[MAX_KEYS];
  uint8_t queued_ = 0;
  uint32_t first_pressed_ = 0;
  Active active_[MAX_ACTIVE];
  uint8_t active_count_ = 0;
  bool flushing_ = false;

  static void load();
  static void parse(const uint16_t *map);
  static void send_map();
  static void read_map();
  static bool on_layer(const Chord &chord, uint8_t layer);
  static bool has_key(const Chord &chord, uint8_t position);
  static uint16_t timeout_of(const Chord &chord);

  bool in_queue(KeyAddr addr) const;
  bool queue_within(const Chord &chord) const;
  int8_t active_holding(KeyAddr addr, uint8_t &index) const;
  void push(KeyAddr addr, Key key);
  void flush();
  void decide();
  void fire(uint8_t chord);
  void inject(Key key, KeyAddr addr, uint8_t state);
};

}  // namespace plugin
}  // namespace kaleidoscope

extern kaleidoscope::plugin::Chords Chords;
