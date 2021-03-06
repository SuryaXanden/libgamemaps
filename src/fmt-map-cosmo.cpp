/**
 * @file  fmt-map-cosmo.cpp
 * @brief MapType and Map2D implementation for Cosmo levels.
 *
 * This file format is fully documented on the ModdingWiki:
 *   http://www.shikadi.net/moddingwiki/Cosmo_Level_Format
 *
 * Copyright (C) 2010-2015 Adam Nielsen <malvineous@shikadi.net>
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

#include <camoto/iostream_helpers.hpp>
#include <camoto/util.hpp> // make_unique
#include <camoto/gamegraphics/image-memory.hpp>
#include "map-core.hpp"
#include "map2d-core.hpp"
#include "fmt-map-cosmo.hpp"

/// Width of each tile in pixels
#define CCA_TILE_WIDTH 8

/// Height of each tile in pixels
#define CCA_TILE_HEIGHT 8

/// Maximum width of a valid level, in tiles (TODO: See if the game has a maximum value)
#define CCA_MAX_WIDTH 512

/// Maximum number of actors in a valid level (TODO: See if the game has a maximum value)
#define CCA_MAX_ACTORS 512

/// Length of the map data, in bytes
#define CCA_LAYER_LEN_BG 65528

/// Number of tiles in the map
#define CCA_NUM_TILES_BG (CCA_LAYER_LEN_BG / 2)

/// Number of tiles in the solid tileset
#define CCA_NUM_SOLID_TILES 2000

/// Number of tiles in the masked tileset
#define CCA_NUM_MASKED_TILES 1000

/// Map code to write for locations with no tile set
#define CCA_DEFAULT_BGTILE     0x00

// Indices into attributes array
#define ATTR_BACKDROP 0
#define ATTR_RAIN     1
#define ATTR_SCROLL_X 2
#define ATTR_SCROLL_Y 3
#define ATTR_PAL_ANIM 4
#define ATTR_MUSIC    5

namespace camoto {
namespace gamemaps {

using namespace camoto::gamegraphics;

class Layer_Cosmo_Actors: public Map2DCore::LayerCore
{
	public:
		Layer_Cosmo_Actors(stream::input& content, stream::input& actrinfo,
			stream::pos& lenMap)
		{
			this->readActorHeights(actrinfo);

			// Read in the actor layer
			uint16_t numActorInts;
			content >> u16le(numActorInts);
			unsigned int numActors = numActorInts / 3;
			if (lenMap < numActors * 6) throw stream::error("Map file has been truncated!");
			this->v_allItems.reserve(numActors);
			for (unsigned int i = 0; i < numActors; i++) {
				Item t;
				t.type = Item::Type::Default;
				content
					>> u16le(t.code)
					>> u16le(t.pos.x)
					>> u16le(t.pos.y)
				;

				// Sprite coordinates are for the bottom-left tile, but Camoto uses the
				// top-left, so we have to adjust the sprites based on their height.
				int index = this->actorCodeToTileIndex(t.code);
				if ((index >= 0) && ((unsigned int)index < this->actorHeight.size())) {
					t.pos.y -= this->actorHeight[index] - 1;
				}

				switch (t.code) {
					case 295: // falling star
						t.type = Item::Type::Movement;
						t.movementFlags = Item::MovementFlags::DistanceLimit;
						t.movementDistLeft = 0;
						t.movementDistRight = 0;
						t.movementDistUp = 0;
						t.movementDistDown = Item::DistIndeterminate;
						t.code = 31 + 1; // normal star image
						break;
				}
				this->v_allItems.push_back(t);
			}
			lenMap -= 6 * numActors;
		}

		virtual ~Layer_Cosmo_Actors()
		{
		}

		void flush(stream::output& content, const Point& mapSize)
		{
			// Write the actor layer
			auto numActorInts = this->v_allItems.size() * 3;
			if (numActorInts > 32767) throw camoto::error("Too many actors - max 32767.");
			content << u16le(numActorInts);

			for (auto& i : this->v_allItems) {
				assert((i.pos.x < mapSize.x) && (i.pos.y < mapSize.y));
				uint16_t finalCode = i.code;

				// Map any falling actors to the correct code
				if (
					(i.type & Item::Type::Movement)
					&& (i.movementFlags & Item::MovementFlags::DistanceLimit)
					&& (i.movementDistDown == Item::DistIndeterminate)
				) {
					switch (i.code) {
						case 31 + 1: finalCode = 295; break; // falling star
					}
				}
				content
					<< u16le(finalCode)
					<< u16le(i.pos.x)
					<< u16le(i.pos.y)
				;
			}
			return;
		}

		virtual std::string title() const
		{
			return "Actors";
		}

		virtual Caps caps() const
		{
			return Caps::UseImageDims;
		}

		virtual ImageFromCodeInfo imageFromCode(const Item& item,
			const TilesetCollection& tileset) const
		{
			ImageFromCodeInfo ret;

			auto t = tileset.find(ImagePurpose::SpriteTileset1);
			if (t == tileset.end()) { // no tileset?!
				ret.type = ImageFromCodeInfo::ImageType::Unknown;
				return ret;
			}

			int index = this->actorCodeToTileIndex(item.code);
			if (index < 0) {
				ret.type = ImageFromCodeInfo::ImageType::HexDigit;
				ret.digit = 0x100 + item.code; // 0x100 means display two-digit hex
				return ret;
			}

			auto& images = t->second->files();
			unsigned int num = images.size();
			if ((unsigned int)index >= num) { // out of range
				ret.type = ImageFromCodeInfo::ImageType::Unknown;
				return ret;
			}
			while (!(images[index]->fAttr & gamearchive::Archive::File::Attribute::Folder)) {
				// Some images are duplicated, but libgamegraphics reports these as
				// empty tilesets.  So if we encounter an empty one, find the next
				// available actor.
				index++;
				if ((unsigned int)index >= num) { // out of range
					ret.type = ImageFromCodeInfo::ImageType::Unknown;
					return ret;
				}
			}

			auto tsActor = t->second->openTileset(images[index]);
			auto& actorFrames = tsActor->files();
			if (actorFrames.size() <= 0) { // no images
				ret.type = ImageFromCodeInfo::ImageType::Unknown;
				return ret;
			}

			ret.img = tsActor->openImage(actorFrames[0]);
			ret.type = ImageFromCodeInfo::ImageType::Supplied;

			// Special image tweaks
			switch (item.code) {
				case 127: { // Pink eye plant (upside down)
					auto pix = ret.img->convert();
					auto mask = ret.img->convert_mask();
					auto len = pix.size();
					Pixels pix_flip(len);
					Pixels mask_flip(len);
					auto pix_s = pix.data();
					auto mask_s = mask.data();
					auto pix_d = pix_flip.data() + pix_flip.size();
					auto mask_d = mask_flip.data() + mask_flip.size();
					for (unsigned int i = 0; i < len; i++) {
						*--pix_d = *pix_s++;
						*--mask_d = *mask_s++;
					}
					ret.img = std::make_unique<Image_Memory>(
						ret.img->dimensions(),
						pix_flip,
						mask_flip,
						Point{0, 0},
						Point{0, 0},
						nullptr
					);
					break;
				}
			}
			return ret;
		}

		virtual std::vector<Item> availableItems() const
		{
			std::vector<Item> validItems;
			/// @todo Populate proper item list
			for (int i = 0; i < 10; i++) {
				validItems.emplace_back();
				auto& t = validItems.back();
				t.type = Item::Type::Default;
				t.pos = {0, 0};
				t.code = i + 31;
			}
			return validItems;
		}

		/// Read in the actor info, so we can find the height of each actor
		void readActorHeights(stream::input& content)
		{
			auto lenContent = content.size();
			content.seekg(0, stream::start);
			unsigned int nextOffset;
			content >> u16le(nextOffset);

			unsigned int numImages = nextOffset;
			// The file data is loaded in lots of 65535 bytes, into memory blocks of
			// 65536 bytes.  This means after every 65535 bytes, a padding byte should be
			// inserted in order for the offsets to add up correctly.  Likewise when
			// saving data, every 65536th byte should be dropped.
			nextOffset *= 2;
			nextOffset -= nextOffset / 65536;
			if (lenContent < numImages * 2) {
				throw stream::error("Actor info FAT truncated");
			}
			std::vector<stream::pos> offsets;
			offsets.reserve(numImages);
			for (unsigned int i = 0; i < numImages; i++) {
				offsets.push_back(nextOffset);
				if (i == numImages - 1) {
					nextOffset = lenContent;
				} else {
					content >> u16le(nextOffset);
					nextOffset *= 2;
					nextOffset -= nextOffset / 65536;
				}
			}
			// Now read the sizes
			uint16_t height;
			for (auto i : offsets) {
				content.seekg(i, stream::start);
				content >> u16le(height);
				this->actorHeight.push_back(height);
			}
			return;
		}

		/// Convert an actor code from the map file to an actrinfo index.
		/**
		 * This is used both when loading and saving the actor layer (to
		 * correctly adjust the height of the actor sprites) as well as
		 * when mapping tile codes back to images.
		 */
		int actorCodeToTileIndex(unsigned int code) const
		{
			if (code < 32) return -1;
			return code - 31;
		}

	protected:
		/// Height of each actor frame, in tiles
		std::vector<unsigned int> actorHeight;
};

class Layer_Cosmo_Background: public Map2DCore::LayerCore
{
	public:
		Layer_Cosmo_Background(stream::input& content, stream::pos& lenMap,
			unsigned int mapWidth)
		{
			// Read the background layer
			this->v_allItems.reserve(CCA_NUM_TILES_BG);

			for (unsigned int i = 0; (i < CCA_NUM_TILES_BG) && (lenMap >= 2); i++) {
				uint16_t code;
				content >> u16le(code);

				// Don't push zero codes (these are transparent/no-tile)
				if (code == CCA_DEFAULT_BGTILE) continue;

				this->v_allItems.emplace_back();
				auto& t = this->v_allItems.back();

				t.type = Item::Type::Default;
				t.pos.x = i % mapWidth;
				t.pos.y = i / mapWidth;
				t.code = code;
				lenMap -= 2;
			}
		}

		virtual ~Layer_Cosmo_Background()
		{
		}

		void flush(stream::output& content, const Point& mapSize)
		{
			// Write the background layer
			unsigned long lenBG = mapSize.x * mapSize.y;
			std::vector<uint16_t> bg(lenBG, CCA_DEFAULT_BGTILE);

			for (auto& i : this->items()) {
				if ((i.pos.x >= mapSize.x) || (i.pos.y >= mapSize.y)) {
					throw stream::error("Layer has tiles outside map boundary!");
				}
				bg[i.pos.y * mapSize.x + i.pos.x] = i.code;
			}

			for (auto& i : bg) content << u16le(i);
			return;
		}

		virtual std::string title() const
		{
			return "Background";
		}

		virtual Caps caps() const
		{
			return Caps::Default;
		}

		virtual ImageFromCodeInfo imageFromCode(const Item& item,
			const TilesetCollection& tileset) const
		{
			ImageFromCodeInfo ret;

			unsigned int index = item.code >> 3; // divide by 8
			ImagePurpose purpose;
			if (index >= CCA_NUM_SOLID_TILES) {
				index -= CCA_NUM_SOLID_TILES;
				index /= 5;
				/*if (index >= 1000) {
					// out of range!
					return Map2D::Layer::Unknown;
				}*/
				purpose = ImagePurpose::ForegroundTileset1;
			} else {
				purpose = ImagePurpose::BackgroundTileset1;
			}

			auto t = tileset.find(purpose);
			if (t == tileset.end()) { // no tileset?!
				ret.type = ImageFromCodeInfo::ImageType::Unknown;
				return ret;
			}

			auto& images = t->second->files();
			if (index >= images.size()) { // out of range
				ret.type = ImageFromCodeInfo::ImageType::Unknown;
				return ret;
			}

			ret.img = t->second->openImage(images[index]);
			ret.type = ImageFromCodeInfo::ImageType::Supplied;
			return ret;
		}

		virtual std::vector<Item> availableItems() const
		{
			std::vector<Item> validItems;
			for (unsigned int i = 0; i < CCA_NUM_SOLID_TILES; i++) {
				if (i == CCA_DEFAULT_BGTILE) continue;

				validItems.emplace_back();
				auto& t = validItems.back();

				t.type = Map2D::Layer::Item::Type::Default;
				t.pos = {0, 0};
				t.code = i << 3;
			}
			for (unsigned int i = 0; i < CCA_NUM_MASKED_TILES; i++) {
				validItems.emplace_back();
				auto& t = validItems.back();

				t.type = Map2D::Layer::Item::Type::Default;
				t.pos = {0, 0};
				t.code = (CCA_NUM_SOLID_TILES + i * 5) << 3;
			}
			return validItems;
		}
};

class Map_Cosmo: public MapCore, public Map2DCore
{
	public:
		Map_Cosmo(std::unique_ptr<stream::inout> content, stream::input& actrinfo)
			:	content(std::move(content))
		{
			stream::pos lenMap = this->content->size();
			this->content->seekg(0, stream::start);

			uint16_t flags;
			*this->content
				>> u16le(flags)
				>> u16le(this->mapWidth)
			;
			lenMap -= 4;

			// Set the attributes
			assert(this->v_attributes.size() == ATTR_BACKDROP); // make sure compile-time index is correct
			this->v_attributes.emplace_back();
			auto& attrBackdrop = this->v_attributes.back();
			attrBackdrop.type = Attribute::Type::Enum;
			attrBackdrop.name = "Backdrop";
			attrBackdrop.desc = "Index of backdrop to draw behind level.";
			attrBackdrop.enumValue = flags & 0x1F;
			attrBackdrop.enumValueNames = {
				"0 - Blank (bdblank.mni)",
				"1 - Pipe (bdpipe.mni)",
				"2 - Red Sky (bdredsky.mni)",
				"3 - Rock (bdrocktk.mni)",
				"4 - Jungle (bdjungle.mni)",
				"5 - Star (bdstar.mni)",
				"6 - Weird (bdwierd.mni)",
				"7 - Cave (bdcave.mni)",
				"8 - Ice (bdice.mni)",
				"9 - Shrum (bdshrum.mni)",
				"10 - Tech (bdtechms.mni)",
				"11 - New sky (bdnewsky.mni)",
				"12 - Star 2 (bdstar2.mni)",
				"13 - Star 3 (bdstar3.mni)",
				"14 - Forest (bdforest.mni)",
				"15 - Mountain (bdmountn.mni)",
				"16 - Guts (bdguts.mni)",
				"17 - Broken Tech (bdbrktec.mni)",
				"18 - Clouds (bdclouds.mni)",
				"19 - Future city (bdfutcty.mni)",
				"20 - Ice 2 (bdice2.mni)",
				"21 - Cliff (bdcliff.mni)",
				"22 - Spooky (bdspooky.mni)",
				"23 - Crystal (bdcrystl.mni)",
				"24 - Circuit (bdcircut.mni)",
				"25 - Circuit PC (bdcircpc.mni)",
			};

			assert(this->v_attributes.size() == ATTR_RAIN); // make sure compile-time index is correct
			this->v_attributes.emplace_back();
			auto& attrRain = this->v_attributes.back();
			attrRain.type = Attribute::Type::Enum;
			attrRain.name = "Rain";
			attrRain.desc = "Is it raining in this level?";
			attrRain.enumValue = (flags >> 5) & 1;
			attrRain.enumValueNames = {"No", "Yes"};

			assert(this->v_attributes.size() == ATTR_SCROLL_X); // make sure compile-time index is correct
			this->v_attributes.emplace_back();
			auto& attrScrollX = this->v_attributes.back();
			attrScrollX.type = Attribute::Type::Enum;
			attrScrollX.name = "Scroll X";
			attrScrollX.desc = "Should the backdrop scroll horizontally?";
			attrScrollX.enumValue = (flags >> 6) & 1;
			attrScrollX.enumValueNames = {"No", "Yes"};

			assert(this->v_attributes.size() == ATTR_SCROLL_Y); // make sure compile-time index is correct
			this->v_attributes.emplace_back();
			auto& attrScrollY = this->v_attributes.back();
			attrScrollY.type = Attribute::Type::Enum;
			attrScrollY.name = "Scroll Y";
			attrScrollY.desc = "Should the backdrop scroll vertically?";
			attrScrollY.enumValue = (flags >> 7) & 1;
			attrScrollY.enumValueNames = {"No", "Yes"};

			assert(this->v_attributes.size() == ATTR_PAL_ANIM); // make sure compile-time index is correct
			this->v_attributes.emplace_back();
			auto& attrPalAnim = this->v_attributes.back();
			attrPalAnim.type = Attribute::Type::Enum;
			attrPalAnim.name = "Palette animation";
			attrPalAnim.desc = "Type of colour animation to use in this level.  Only "
				"dark magenta (EGA colour 5) is animated.";
			attrPalAnim.enumValue = (flags >> 8) & 7;
			attrPalAnim.enumValueNames = {
				"0 - No animation",
				"1 - Lightning",
				"2 - Cycle: red -> yellow -> white",
				"3 - Cycle: red -> green -> blue",
				"4 - Cycle: black -> grey -> white",
				"5 - Flashing: red -> magenta -> white",
				"6 - Dark magenta -> black, bomb trigger",
				"7 - Unknown/unused",
			};

			assert(this->v_attributes.size() == ATTR_MUSIC); // make sure compile-time index is correct
			this->v_attributes.emplace_back();
			auto& attrMusic = this->v_attributes.back();
			attrMusic.type = Attribute::Type::Enum;
			attrMusic.name = "Music";
			attrMusic.desc = "Index of the song to play as background music in the level.";
			attrMusic.enumValue = flags >> 11;
			attrMusic.enumValueNames = {
				"0 - Caves (mcaves.mni)",
				"1 - Scarry (mscarry.mni)",
				"2 - Boss (mboss.mni)",
				"3 - Run Away (mrunaway.mni)",
				"4 - Circus (mcircus.mni)",
				"5 - Tech World (mtekwrd.mni)",
				"6 - Easy Level (measylev.mni)",
				"7 - Rock It (mrockit.mni)",
				"8 - Happy (mhappy.mni)",
				"9 - Devo (mdevo.mni)",
				"10 - Dadoda (mdadoda.mni)",
				"11 - Bells (mbells.mni)",
				"12 - Drums (mdrums.mni)",
				"13 - Banjo (mbanjo.mni)",
				"14 - Easy 2 (measy2.mni)",
				"15 - Tech 2 (mteck2.mni)",
				"16 - Tech 3 (mteck3.mni)",
				"17 - Tech 4 (mteck4.mni)",
				"18 - ZZ Top (mzztop.mni)",
			};

			// Read in the actor layer
			auto layerAC = std::make_shared<Layer_Cosmo_Actors>(
				*this->content, actrinfo, lenMap
			);

			// Read the background layer
			auto layerBG = std::make_shared<Layer_Cosmo_Background>(
				*this->content, lenMap, mapWidth
			);

			// Add the layers in the opposite order to what they are in the file, so
			// the Z-order is correct.
			this->v_layers.push_back(layerBG);
			this->v_layers.push_back(layerAC);
		}

		virtual ~Map_Cosmo()
		{
		}

		virtual void flush()
		{
			assert(this->layers().size() == 2);

			auto mapSize = this->mapSize();

			assert(this->v_attributes.size() == 6);

			assert(this->v_attributes[ATTR_BACKDROP].type == Attribute::Type::Enum);
			assert(this->v_attributes[ATTR_RAIN    ].type == Attribute::Type::Enum);
			assert(this->v_attributes[ATTR_SCROLL_X].type == Attribute::Type::Enum);
			assert(this->v_attributes[ATTR_SCROLL_Y].type == Attribute::Type::Enum);
			assert(this->v_attributes[ATTR_PAL_ANIM].type == Attribute::Type::Enum);
			assert(this->v_attributes[ATTR_MUSIC   ].type == Attribute::Type::Enum);

			uint16_t flags =
				   this->v_attributes[ATTR_BACKDROP].enumValue
				| (this->v_attributes[ATTR_RAIN    ].enumValue << 5)
				| (this->v_attributes[ATTR_SCROLL_X].enumValue << 6)
				| (this->v_attributes[ATTR_SCROLL_Y].enumValue << 7)
				| (this->v_attributes[ATTR_PAL_ANIM].enumValue << 8)
				| (this->v_attributes[ATTR_MUSIC   ].enumValue << 11)
			;

			this->content->seekp(0, stream::start);
			*this->content
				<< u16le(flags)
				<< u16le(mapSize.x)
			;

			// Write the actor layer
			auto layerAC = dynamic_cast<Layer_Cosmo_Actors*>(this->v_layers[1].get());
			layerAC->flush(*this->content, mapSize);

			// Write the background layer
			auto layerBG = dynamic_cast<Layer_Cosmo_Background*>(this->v_layers[0].get());
			layerBG->flush(*this->content, mapSize);

			this->content->flush();
			return;
		}

		virtual std::map<ImagePurpose, GraphicsFilename> graphicsFilenames() const
		{
			// Populate the graphics filenames
			GraphicsFilename gf;
			gf.type = "img-cosmo-backdrop";

			switch (this->v_attributes.at(ATTR_BACKDROP).enumValue) {
				case 0: gf.filename = "bdblank.mni"; break;
				case 1: gf.filename = "bdpipe.mni"; break;
				case 2: gf.filename = "bdredsky.mni"; break;
				case 3: gf.filename = "bdrocktk.mni"; break;
				case 4: gf.filename = "bdjungle.mni"; break;
				case 5: gf.filename = "bdstar.mni"; break;
				case 6: gf.filename = "bdwierd.mni"; break;
				case 7: gf.filename = "bdcave.mni"; break;
				case 8: gf.filename = "bdice.mni"; break;
				case 9: gf.filename = "bdshrum.mni"; break;
				case 10: gf.filename = "bdtechms.mni"; break;
				case 11: gf.filename = "bdnewsky.mni"; break;
				case 12: gf.filename = "bdstar2.mni"; break;
				case 13: gf.filename = "bdstar3.mni"; break;
				case 14: gf.filename = "bdforest.mni"; break;
				case 15: gf.filename = "bdmountn.mni"; break;
				case 16: gf.filename = "bdguts.mni"; break;
				case 17: gf.filename = "bdbrktec.mni"; break;
				case 18: gf.filename = "bdclouds.mni"; break;
				case 19: gf.filename = "bdfutcty.mni"; break;
				case 20: gf.filename = "bdice2.mni"; break;
				case 21: gf.filename = "bdcliff.mni"; break;
				case 22: gf.filename = "bdspooky.mni"; break;
				case 23: gf.filename = "bdcrystl.mni"; break;
				case 24: gf.filename = "bdcircut.mni"; break;
				case 25: gf.filename = "bdcircpc.mni"; break;
				default: gf.filename = "bdblank.mni"; break;
			}
			return {
				std::make_pair(ImagePurpose::BackgroundImage, gf),
				std::make_pair(
					ImagePurpose::BackgroundTileset1,
					GraphicsFilename{"tiles.mni", "tls-cosmo"}
				),
				std::make_pair(
					ImagePurpose::ForegroundTileset1,
					GraphicsFilename{"masktile.mni", "tls-cosmo-masked"}
				),
				std::make_pair(
					ImagePurpose::SpriteTileset1,
					GraphicsFilename{"actrinfo.mni", "tls-cosmo-actrinfo"}
				),
			};
		}

		virtual Caps caps() const
		{
			return
				Map2D::Caps::HasViewport
				| Map2D::Caps::HasMapSize
				| Map2D::Caps::HasTileSize
			;
		}

		virtual Point viewport() const
		{
			return {304, 144};
		}

		virtual Point mapSize() const
		{
			return {(long)this->mapWidth, (long)(32768 / this->mapWidth)};
		}

		virtual Point tileSize() const
		{
			return {CCA_TILE_WIDTH, CCA_TILE_HEIGHT};
		}

		Background background(const TilesetCollection& tileset) const
		{
			return this->backgroundUseBGImage(tileset);
		}

	private:
		std::unique_ptr<stream::inout> content;
		unsigned int mapWidth;
};


std::string MapType_Cosmo::code() const
{
	return "map2d-cosmo";
}

std::string MapType_Cosmo::friendlyName() const
{
	return "Cosmo's Cosmic Adventures level";
}

std::vector<std::string> MapType_Cosmo::fileExtensions() const
{
	return {"mni"};
}

std::vector<std::string> MapType_Cosmo::games() const
{
	return {"Cosmo's Cosmic Adventures"};
}

MapType::Certainty MapType_Cosmo::isInstance(stream::input& content) const
{
	stream::pos lenMap = content.size();

	// TESTED BY: fmt_map_cosmo_isinstance_c01/c02
	if (lenMap < 6 + CCA_LAYER_LEN_BG) return MapType::DefinitelyNo; // too short

	uint16_t mapWidth;
	content.seekg(2, stream::start);
	content >> u16le(mapWidth);

	// TESTED BY: fmt_map_cosmo_isinstance_c03
	if (mapWidth > CCA_MAX_WIDTH) return MapType::DefinitelyNo; // map too wide

	uint16_t numActorInts;
	content >> u16le(numActorInts);

	// TESTED BY: fmt_map_cosmo_isinstance_c04
	if (numActorInts > (CCA_MAX_ACTORS * 3)) return MapType::DefinitelyNo; // too many actors

	// TESTED BY: fmt_map_cosmo_isinstance_c05
	if ((unsigned)(6 + numActorInts * 3) > lenMap) {
		// This doesn't count the BG layer, because it seems to be possible for
		// it to be an arbitrary size - missing tiles are just left as blanks
		return MapType::DefinitelyNo; // file too small
	}

	// TODO: Read map data and confirm each uint16le is < 56000

	// TESTED BY: fmt_map_cosmo_isinstance_c00
	return MapType::DefinitelyYes;
}

std::unique_ptr<Map> MapType_Cosmo::create(
	std::unique_ptr<stream::inout> content, SuppData& suppData) const
{
	// TODO: Implement
	throw stream::error("Not implemented yet!");
}

std::unique_ptr<Map> MapType_Cosmo::open(
	std::unique_ptr<stream::inout> content, SuppData& suppData) const
{
	auto suppActrInfo = suppData.find(SuppItem::Extra1);
	if (suppActrInfo == suppData.end()) {
		throw camoto::error("Missing content for Extra1 (actor info) "
			"supplementary item.");
	}
	return std::make_unique<Map_Cosmo>(std::move(content), *(suppActrInfo->second));
}

SuppFilenames MapType_Cosmo::getRequiredSupps(stream::input& content,
	const std::string& filename) const
{
	SuppFilenames supps;
	supps[SuppItem::Extra1] = "actrinfo.mni";
	return supps;
}

} // namespace gamemaps
} // namespace camoto
