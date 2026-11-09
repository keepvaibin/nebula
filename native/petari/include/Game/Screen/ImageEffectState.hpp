#pragma once


class ImageEffectBase;
class ImageEffectDirector;

class ImageEffectState {
public:
    /// @brief Creates a new `ImageEffectState`.
    /// @param pHost A pointer to the owning `ImageEffectDirector` instance.
    ImageEffectState(ImageEffectDirector* pHost) : mHost(pHost) {
    }

    /// @brief Destroys the `ImageEffectState`.
    virtual ~ImageEffectState() {
    }

    virtual void update();
    virtual bool doesEffectExist() const = 0;
    virtual ImageEffectBase* getEffect() const = 0;
    virtual void onChange() {
    }

    void forceOff();

private:
    ImageEffectDirector* mHost;
};

namespace ImageEffectStateImpl {
    class StateNull : public ImageEffectState {
    public:
        /// @brief Creates a new `StateNull`.
        /// @param pHost A pointer to the owning `ImageEffectDirector` instance.
        StateNull(ImageEffectDirector* pHost) : ImageEffectState(pHost) {
        }

        virtual bool doesEffectExist() const override {
            return true;
        }

        virtual ImageEffectBase* getEffect() const override {
            return nullptr;
        }
    };

    class StateBloomNormal : public ImageEffectState {
    public:
        /// @brief Creates a new `StateBloomNormal`.
        /// @param pHost A pointer to the owning `ImageEffectDirector` instance.
        StateBloomNormal(ImageEffectDirector* pHost);

        virtual void update() override;
        virtual bool doesEffectExist() const override;
        virtual ImageEffectBase* getEffect() const override;
        virtual void onChange() override;

        void setBloomIntensity(u8 bloomIntensity);
        void setThreshold(u8 threshold);
        void setIntensity1(u8 intensity1);
        void setIntensity1Default();
        void setIntensity2(u8 intensity2);
        void setIntensity2Default();

    private:
        bool mIsSnap;  // the next update jumps to the set values instead of easing toward them
        f32 mBloomIntensityNow;
        u8 mBloomIntensity;
        f32 mThresholdNow;
        u8 mThreshold;
        f32 mIntensity1Now;
        s32 mIntensity1;
        f32 mIntensity2Now;
        s32 mIntensity2;
    };

    class StateBloomSimple : public ImageEffectState {
    public:
        /// @brief Creates a new `StateBloomSimple`.
        /// @param pHost A pointer to the owning `ImageEffectDirector` instance.
        StateBloomSimple(ImageEffectDirector* pHost) : ImageEffectState(pHost) {
        }

        virtual bool doesEffectExist() const override;
        virtual ImageEffectBase* getEffect() const override;

        void setMaskFilterAll();
        void setMaskFilterRed();
        void setMaskFilterGreen();
        void setMaskFilterBlue();
        void setThreshold(u8 threshold);
        void setIntensity(f32 intensity);
    };

    class StateScreenBlur : public ImageEffectState {
    public:
        /// @brief Creates a new `StateScreenBlur`.
        /// @param pHost A pointer to the owning `ImageEffectDirector` instance.
        StateScreenBlur(ImageEffectDirector* pHost) : ImageEffectState(pHost) {
        }

        virtual bool doesEffectExist() const override;
        virtual ImageEffectBase* getEffect() const override;

        void setIntensity(f32 intensity);
    };

    class StateDepthOfField : public ImageEffectState {
    public:
        /// @brief Creates a new `StateDepthOfField`.
        /// @param pHost A pointer to the owning `ImageEffectDirector` instance.
        StateDepthOfField(ImageEffectDirector* pHost) : ImageEffectState(pHost) {
        }

        virtual bool doesEffectExist() const override;
        virtual ImageEffectBase* getEffect() const override;

        void setIntensity(f32 intensity);
        void setBlurMaxDist(s32 blurMaxDist);
        void setBlurMinDist(s32 blurMinDist);
    };
};  // namespace ImageEffectStateImpl
