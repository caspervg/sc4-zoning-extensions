#include "SC4ZoningExtensionsDirector.hpp"

static SC4ZoningExtensionsDirector sDirector;

cRZCOMDllDirector* RZGetCOMDllDirector()
{
    static auto sAddedRef = false;
    if (!sAddedRef) {
        sDirector.AddRef();
        sAddedRef = true;
    }
    return &sDirector;
}
