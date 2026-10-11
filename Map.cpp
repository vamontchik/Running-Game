#include "Map.h"

Map::Map()
{
}

Map::~Map()
{
}

void Map::addBlock(const Block& block)
{
    blocks.push_back(block);
}

bool Map::removeBlock(size_t index)
{
    if (index >= blocks.size())
    {
        return false;
    }

    blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

const Block& Map::getBlock(size_t index) const
{
    static const Block fallback;
    return index < blocks.size() ? blocks[index] : fallback;
}

size_t Map::getBlockCount() const
{
    return blocks.size();
}

void Map::clear()
{
    blocks.clear();
}
