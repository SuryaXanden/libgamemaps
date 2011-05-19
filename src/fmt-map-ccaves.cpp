/**
 * @file   fmt-map-ccaves.cpp
 * @brief  MapType and Map2D implementation for Crystal Caves levels.
 *
 * This file format is fully documented on the ModdingWiki:
 *   http://www.shikadi.net/moddingwiki/Crystal_Caves
 *
 * Copyright (C) 2010-2011 Adam Nielsen <malvineous@shikadi.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/progress.hpp>
#include <boost/shared_array.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <exception>
#include <string.h>

#include "fmt-map-ccaves.hpp"
#include <camoto/iostream_helpers.hpp>
#include <camoto/debug.hpp>

#define CC_MAP_WIDTH            40
#define CC_TILE_WIDTH           16
#define CC_TILE_HEIGHT          16

/// This is the largest number of rows ever expected to be seen.
#define CC_MAX_MAP_HEIGHT      100

/// This is the largest valid tile code in the background layer.
#define CC_MAX_VALID_TILECODE   0xc0

/// Width of map view during gameplay, in pixels
#define CC_VIEWPORT_WIDTH       320

/// Height of map view during gameplay, in pixels
#define CC_VIEWPORT_HEIGHT      192

namespace camoto {
namespace gamemaps {

std::string CCavesMapType::getMapCode() const
	throw ()
{
	return "map-ccaves";
}

std::string CCavesMapType::getFriendlyName() const
	throw ()
{
	return "Crystal Caves level";
}

std::vector<std::string> CCavesMapType::getFileExtensions() const
	throw ()
{
	std::vector<std::string> vcExtensions;
	vcExtensions.push_back("ccl");
	return vcExtensions;
}

std::vector<std::string> CCavesMapType::getGameList() const
	throw ()
{
	std::vector<std::string> vcGames;
	vcGames.push_back("Crystal Caves");
	return vcGames;
}

MapType::Certainty CCavesMapType::isInstance(istream_sptr psMap) const
	throw (std::ios::failure)
{
	psMap->seekg(0, std::ios::end);
	io::stream_offset lenMap = psMap->tellg();

	psMap->seekg(0, std::ios::beg);

	// TESTED BY: fmt_map_ccaves_isinstance_c01
	if (lenMap < CC_MAP_WIDTH + 1) return MapType::DefinitelyNo; // too small

	uint8_t row[CC_MAP_WIDTH];
	int y;
	for (y = 0; (y < CC_MAX_MAP_HEIGHT) && lenMap; y++) {
		uint8_t lenRow;
		psMap >> u8(lenRow);
		lenMap--;

		// Incorrect row length
		// TESTED BY: fmt_map_ccaves_isinstance_c02
		if (lenRow != CC_MAP_WIDTH) return MapType::DefinitelyNo;

		// Incomplete row
		// TESTED BY: fmt_map_ccaves_isinstance_c03
		if (lenMap < CC_MAP_WIDTH) return MapType::DefinitelyNo;

		// Ensure the row data is valid
		psMap->read((char *)row, CC_MAP_WIDTH);
		for (int x = 0; x < CC_MAP_WIDTH; x++) {
			// TESTED BY: fmt_map_ccaves_isinstance_c04
			if (row[x] > CC_MAX_VALID_TILECODE) return MapType::DefinitelyNo; // invalid tile
		}

		lenMap -= CC_MAP_WIDTH;
	}

	// TESTED BY: fmt_map_ccaves_isinstance_c05
	if (y == CC_MAX_MAP_HEIGHT) return MapType::DefinitelyNo; // map too tall

	// TESTED BY: fmt_map_ccaves_isinstance_c00
	return MapType::DefinitelyYes;
}

MapPtr CCavesMapType::create(SuppData& suppData) const
	throw (std::ios::failure)
{
	// TODO: Implement
	throw std::ios::failure("Not implemented yet!");
}

MapPtr CCavesMapType::open(istream_sptr input, SuppData& suppData) const
	throw (std::ios::failure)
{
	input->seekg(0, std::ios::end);
	io::stream_offset lenMap = input->tellg();

	input->seekg(0, std::ios::beg);

	// Read the background layer
	uint8_t *bg = new uint8_t[lenMap];
	boost::shared_ptr<uint8_t> sbg(bg);
	input->read((char *)bg, lenMap);

	int height = lenMap / (CC_MAP_WIDTH + 1);

	Map2D::Layer::ItemPtrVectorPtr tiles(new Map2D::Layer::ItemPtrVector());
	tiles->reserve(CC_MAP_WIDTH * height);
	for (int y = 0; y < height; y++) {
		bg++; // skip row length byte
		for (int x = 0; x < CC_MAP_WIDTH; x++) {
			Map2D::Layer::ItemPtr t(new Map2D::Layer::Item());
			t->x = x;
			t->y = y;
			t->code = *bg++;
			tiles->push_back(t);
		}
	}
	Map2D::LayerPtr bgLayer(new Map2D::Layer(
		"Background",
		Map2D::Layer::NoCaps,
		0, 0,
		0, 0,
		tiles
	));

	Map2D::LayerPtrVector layers;
	layers.push_back(bgLayer);

	Map2DPtr map(new Map2D(
		Map2D::HasViewport | Map2D::HasGlobalSize | Map2D::HasGlobalTileSize,
		CC_VIEWPORT_WIDTH, CC_VIEWPORT_HEIGHT,
		CC_MAP_WIDTH, height,
		CC_TILE_WIDTH, CC_TILE_HEIGHT,
		layers, Map2D::PathPtrVectorPtr()
	));

	return map;
}

unsigned long CCavesMapType::write(MapPtr map, ostream_sptr output, SuppData& suppData) const
	throw (std::ios::failure)
{
	Map2DPtr map2d = boost::dynamic_pointer_cast<Map2D>(map);
	if (!map2d) throw std::ios::failure("Cannot write this type of map as this format.");
	if (map2d->getLayerCount() != 1)
		throw std::ios::failure("Incorrect layer count for this format.");

	unsigned long lenWritten = 0;
	output->seekp(0, std::ios::beg);

	// Write the background layer
	int mapWidth, mapHeight;
	if (!map2d->getCaps() & Map2D::HasGlobalSize)
		throw std::ios::failure("Cannot write this type of map as this format.");
	map2d->getMapSize(&mapWidth, &mapHeight);
	uint8_t *bg = new uint8_t[mapWidth * mapHeight];
	boost::shared_ptr<uint8_t> sbg(bg);

	Map2D::LayerPtr layer = map2d->getLayer(0);
	const Map2D::Layer::ItemPtrVectorPtr items = layer->getAllItems();
	for (Map2D::Layer::ItemPtrVector::const_iterator i = items->begin();
		i != items->end();
		i++
	) {
		assert(((*i)->x < mapWidth) && ((*i)->y < mapHeight));
		bg[(*i)->y * mapWidth + (*i)->x] = (*i)->code;
	}

	for (int y = 0; y < mapHeight; y++) {
		output << u8(mapWidth);
		output->write((char *)bg, mapWidth);
		bg += mapWidth;
		lenWritten += mapWidth;
	}

	return lenWritten;
}


} // namespace gamemaps
} // namespace camoto