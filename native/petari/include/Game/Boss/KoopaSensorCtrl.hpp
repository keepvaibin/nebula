#pragma once

class Koopa;

class KoopaSensorCtrl {
public:
    /// @param pHost The pointer to the owning Bowser actor instance.
    KoopaSensorCtrl(Koopa* pHost);

    void initSensor();

private:
    /// @brief The pointer to the owning Bowser actor instance.
    Koopa* mHost;
};
