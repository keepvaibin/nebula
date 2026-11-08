#pragma once

class HitSensor;
class Koopa;

class KoopaSensorCtrl {
public:
    /// @param pHost The pointer to the owning Bowser actor instance.
    KoopaSensorCtrl(Koopa* pHost);

    void update(HitSensor* pSensor);
    void initSensor();

private:
    /// @brief The pointer to the owning Bowser actor instance.
    Koopa* mHost;
};
