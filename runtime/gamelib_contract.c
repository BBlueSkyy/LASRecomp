#include "gamelib_contract.h"
#include "address_layout.h"

// Exported even in a stripped APK. This lets the owner build against the actual
// installed runtime, rather than silently assuming the current checkout matches.
__attribute__((visibility("default"), used))
const GameLibBuildContract lasrecomp_build_contract_v1 = {
    {'L', 'A', 'S', 'C'}, 1, GAMELIB_ABI, sizeof(GameLibBuildContract), GUEST_LO, GAMELIB_ID
};

