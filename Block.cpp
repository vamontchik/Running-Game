#include "Block.h"

Block::Block()
    : x(0.0f)
    , y(0.0f)
    , width(0.0f)
    , height(0.0f)
{
}

Block::Block(float x, float y, float width, float height)
    : x(x)
    , y(y)
    , width(width)
    , height(height)
{
}

Block::~Block()
{
}

float Block::getX() const
{
    return x;
}

float Block::getY() const
{
    return y;
}

float Block::getWidth() const
{
    return width;
}

float Block::getHeight() const
{
    return height;
}
