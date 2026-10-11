#ifndef MAP_H
#define MAP_H

#include <vector>

#include "Block.h"

class Map
{
public:
    Map();
    ~Map();

    // Appends a block to the map.
    void addBlock(const Block& block);

    // Removes the block at the given index, if it exists.
    bool removeBlock(size_t index);

    // Returns the block at the given index, or a default block if out of range.
    const Block& getBlock(size_t index) const;

    size_t getBlockCount() const;

    void clear();

private:
    std::vector<Block> blocks;
};

#endif // MAP_H
