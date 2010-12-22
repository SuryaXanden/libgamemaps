/**
 * @file   map2d.cpp
 * @brief  Implementation of base functions in Map2D class.
 *
 * Copyright (C) 2010 Adam Nielsen <malvineous@shikadi.net>
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

#include <camoto/gamemaps/map2d.hpp>

namespace camoto {
namespace gamemaps {

Map2D::Map2D(int caps, int width, int height, int tileWidth, int tileHeight,
	LayerPtrVector& layers)
	throw () :
		caps(caps),
		width(width), height(height),
		tileWidth(tileWidth), tileHeight(tileHeight),
		layers(layers)
{
}

Map2D::~Map2D()
	throw ()
{
}

int Map2D::getCaps()
	throw ()
{
	return this->caps;
}

void Map2D::getMapSize(int *x, int *y)
	throw ()
{
	assert(this->getCaps() & HasGlobalSize);

	*x = this->width;
	*y = this->height;
	return;
}

void Map2D::setMapSize(int x, int y)
	throw ()
{
	assert(this->getCaps() & CanResize);

	this->width = x;
	this->height = y;
	return;
}

void Map2D::getTileSize(int *x, int *y)
	throw ()
{
	assert(this->getCaps() & HasGlobalTileSize);

	*x = this->tileWidth;
	*y = this->tileHeight;
	return;
}

void Map2D::setTileSize(int x, int y)
	throw ()
{
	assert(this->getCaps() & ChangeTileSize);

	this->tileWidth = x;
	this->tileHeight = y;
	return;
}

int Map2D::getLayerCount()
	throw ()
{
	return this->layers.size();
}

Map2D::LayerPtr Map2D::getLayer(int index)
	throw ()
{
	assert(index < this->getLayerCount());
	return this->layers[index];
}

} // namespace gamemaps
} // namespace camoto