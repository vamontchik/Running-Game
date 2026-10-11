#ifndef RUNNER_H
#define RUNNER_H

class Runner
{
public:
    Runner();
    ~Runner();

    // Advances the runner along X by velocity * dt seconds.
    void update(float dt);

    float getX() const;
    float getY() const;
    float getVelocity() const;

    void setX(float newX);
    void setY(float newY);
    void setVelocity(float newVelocity);

private:
    float x;
    float y;
    float velocity;
};

#endif // RUNNER_H
