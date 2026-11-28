#include "TechnoExt.h"
#include <Ext/Techno/Body.h>
#include <Ext/AircraftType/Body.h>
#include <AircraftClass.h>
#include <Utilities/GeneralUtils.h>
#include <vector>

std::vector<CalculateExtraThreatCallback> TechnoExtInterop::CalculateExtraThreatCallbacks = {};
std::vector<CalculateSightCallback> TechnoExtInterop::CalculateSightCallbacks = {};

// Reuse the reinforcement placement calculation after a caller selects a lane.
// S_FALSE means the type has no override; zero/negative values remain meaningful.
DEFINE_EXPORT(HRESULT, Phobos_ApplyAircraftSpawnDistance, AircraftClass* aircraft,
	const CoordStruct* edge, const CoordStruct* target, CoordStruct* result)
{
	if (!aircraft || !aircraft->Type || !edge || !target || !result)
		return E_POINTER;
	auto const typeExt = AircraftTypeExt::TryFetch(aircraft->Type);
	if (!typeExt)
		return E_FAIL;
	*result = *edge;
	if (!typeExt->SpawnDistanceFromTarget.isset())
		return S_FALSE;
	*result = GeneralUtils::CalculateCoordsFromDistance(*edge, *target, typeExt->SpawnDistanceFromTarget.Get());
	return S_OK;
}

DEFINE_EXPORT(HRESULT, ConvertToType_Phobos, FootClass* pThis, TechnoTypeClass* toType)
{
	if (!pThis || !toType)
		return E_POINTER;

	if (!TechnoExt::ConvertToType(pThis, toType))
		return E_INVALIDARG;

	return S_OK;
}

DEFINE_EXPORT(HRESULT, RegisterCalculateExtraThreatCallback, CalculateExtraThreatCallback callback)
{
	if (!callback)
		return E_POINTER;

	TechnoExtInterop::CalculateExtraThreatCallbacks.push_back(callback);
	return S_OK;
}

DEFINE_EXPORT(HRESULT, RegisterCalculateSightCallback, CalculateSightCallback callback)
{
	if (!callback)
		return E_POINTER;

	TechnoExtInterop::CalculateSightCallbacks.push_back(callback);
	return S_OK;
}

// Semantic target classification support for optional water-state providers.
extern "C" __declspec(dllexport) unsigned __cdecl Phobos_GetTargetEnvironmentVersion() { return 2; }
