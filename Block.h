#ifndef BLOCK_H
#define BLOCK_H

class Block
{
public:
    Block();
    Block(float x, float y, float width, float height);
    ~Block();

    float getX() const;
    float getY() const;
    float getWidth() const;
    float getHeight() const;

private:
    float x;
    float y;
    float width;
    float height;
};

#endif // BLOCK_H
