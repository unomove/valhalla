#include "gurka.h"
#include <gtest/gtest.h>

#include "baldr/graphconstants.h"
#include "baldr/graphreader.h"
#include "midgard/pointll.h"
#include "mjolnir/graphtilebuilder.h"

using namespace valhalla;

TEST(shortcuts, test_shortcut_speed) {
  // At C node turn duration is present. As a result the speed for AE shortcut is decreased
  // from 100 kph to 93 kph and for EA shortcut - from 100 kph to 98 kph in the test case below.
  // Similarly truck speed is decreased form 90 kph to 85 kph for AE and from 90 to 89 for EA.
  const std::string ascii_map = R"(A-----B\
                                   |       \C
                                   |        |\
                                   G        | \
                                            F  \
                                                |
                                                |
                                                |
                                                D
                                                |
                                                |
                                                |
                                                E
                                                |
                                                I)";
  const gurka::ways ways = {
      {"ABCDE", {{"highway", "motorway"}, {"maxspeed", "100"}, {"maxspeed:hgv", "90"}}},
      {"CF", {{"highway", "service"}}},
      {"AG", {{"highway", "service"}}},
      {"EI", {{"highway", "service"}}},
  };
  const auto layout = gurka::detail::map_to_coordinates(ascii_map, 10);
  auto map = gurka::buildtiles(layout, ways, {}, {}, "test/data/gurka_shortcut");
  baldr::GraphReader reader(map.config.get_child("mjolnir"));

  std::vector<std::tuple<baldr::GraphId, int, int>> shortcut_infos;
  auto tileset = reader.GetTileSet(0);
  for (const auto tileid : tileset) {
    if (reader.OverCommitted())
      reader.Trim();

    // for each edge in the tile
    const auto* tile = reader.GetGraphTile(tileid);
    for (size_t j = 0; j < tile->header()->directededgecount(); ++j) {
      // skip it if its not a shortcut or the shortcut is one we will never traverse
      const auto* edge = tile->directededge(j);
      if (!edge->is_shortcut() || !(edge->forwardaccess() & baldr::kAutoAccess))
        continue;

      // make a graph id out of the shortcut to send to recover
      auto shortcutid = tileid;
      shortcutid.set_id(j);
      shortcut_infos.push_back(std::make_tuple(shortcutid, edge->speed(), edge->truck_speed()));
    }
  }

  ASSERT_EQ(shortcut_infos.size(), 2);

  for (auto const shortcut_info : shortcut_infos) {
    auto const shortcutid = std::get<0>(shortcut_info);
    auto const shortcut_speed = std::get<1>(shortcut_info);
    auto const shortcut_truck_speed = std::get<2>(shortcut_info);
    auto edgeids = reader.RecoverShortcut(shortcutid);

    // if it gave us back the shortcut we failed
    ASSERT_FALSE(edgeids.front() == shortcutid);

    // Compare the speed on the recovered edges to the speed on the shortcut
    // Shortcut speed should be lower because it is calculated including turn duration
    std::vector<midgard::PointLL> recovered_shape;
    for (auto edgeid : edgeids) {
      const auto* tile = reader.GetGraphTile(edgeid);
      const auto* de = tile->directededge(edgeid);
      EXPECT_GT(de->speed(), shortcut_speed);
      EXPECT_GT(de->truck_speed(), shortcut_truck_speed);
    }
  }
}
