#include "Runner.h"

Runner::Runner()
    : x(0.0f)
    , y(0.0f)
    , velocity(0.0f)
{
}

Runner::~Runner()
{
}

void Runner::update(float dt)
{
    x += velocity * dt;
}

float Runner::getX() const
{
    return x;
}

float Runner::getY() const
{
    return y;
}

float Runner::getVelocity() const
{
    return velocity;
}

void Runner::setX(float newX)
{
    x = newX;
}

void Runner::setY(float newY)
{
    y = newY;
}

void Runner::setVelocity(float newVelocity)
{
    velocity = newVelocity;
}
