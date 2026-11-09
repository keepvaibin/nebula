#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

/// @brief The identifier of a File Selection Screen icon.
class FileSelectIconID {
public:
    /// @brief The identifier of a Mario character icon: the number saved in a user file.
    enum EFellowID {
        /// @brief The Mario icon.
        Mario,

        /// @brief The Luigi icon.
        Luigi,

        /// @brief The Yoshi icon.
        Yoshi,

        /// @brief The Toad icon.
        Kinopio,

        /// @brief The Peach icon.
        Peach,
    };

    FileSelectIconID();

    /// @param rOther The reference to the `FileSelectIconID` to copy from.
    FileSelectIconID(const FileSelectIconID& rOther);

    /// @brief Determines if `this` is equal to `rOther`.
    /// @param rOther A reference to the `FileSelectIconID` to compare with.
    /// @return `true` if `this` is equal to `other`, `false` otherwise.
    bool operator==(const FileSelectIconID& rOther) const {
        return mIsMii == rOther.mIsMii && mData == rOther.mData;
    }

    /// @brief Determines if `this` is inequal to `rOther`.
    /// @param rOther A reference to the `FileSelectIconID` to compare with.
    /// @return `true` if `this` is inequal to `other`, `false` otherwise.
    bool operator!=(const FileSelectIconID& rOther) const {
        return !(*this == rOther);
    }

    /// @brief Updates the value of each member variable.
    /// @param rOther The reference to the `FileSelectIconID` to copy from.
    void set(const FileSelectIconID& rOther);

    /// @brief Names the library Mii at `miiIndex`.
    /// @param miiIndex The new position of the Mii data.
    void setMiiIndex(u32 miiIndex);

    /// @brief Determines if the icon represents a Mii character.
    /// @return `true` if the icon represents a Mii character, `false` otherwise.
    bool isMii() const;

    /// @brief Returns the position of the Mii data in the Mii database.
    /// @return The position of the Mii data.
    u32 getMiiIndex() const;

    /// @brief Updates the identifier of the Mario character.
    /// @param fellowID The new identifier of the Mario character.
    void setFellowID(EFellowID fellowID);

    /// @brief Determines if the icon represents a Mario character.
    /// @return `true` if the icon represents a Mario character, `false` otherwise.
    bool isFellow() const;

    /// @brief Returns the identifier of the Mario character.
    /// @return The identifier of the Mario character.
    EFellowID getFellowID() const;

private:
    /// @brief Determines if the icon represents a Mii character.
    bool mIsMii;

    /// @brief The position of the Mii data in the Mii database or the identifier of a Mario character.
    u32 mData;
};
