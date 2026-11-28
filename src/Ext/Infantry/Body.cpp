#include "Body.h"

#include <BuildingClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <Surface.h>
#include <Drawing.h>
#include <CellSpread.h>
#include <Ext/BuildingType/Body.h>

InfantryExt::ExtContainer InfantryExt::ExtMap;

namespace
{
int CaptureDelayFor(InfantryTypeExt* engineer, BuildingClass* target)
{
	const auto building = BuildingTypeExt::Fetch(target->Type);
	return building ? building->Engineer_CaptureDelay.Get(engineer->Engineer_CaptureDelay.Get())
		: engineer->Engineer_CaptureDelay.Get();
}

void ReportCaptureState(InfantryClass* unit, BuildingClass* target, const char* phase, int remaining)
{
	static unsigned reports = 0;
	if (reports >= 128)
		return;
	++reports;
	Debug::Log("EngineerCapture: revision=4 phase=%s unit=%u target=%u frame=%d remaining=%d human=%d mission=%d queued=%d destination-match=%d target-match=%d sequence=%d.\n",
		phase, unit->UniqueID, target->UniqueID, Unsorted::CurrentFrame, remaining,
		unit->Owner->IsControlledByHuman(), int(unit->CurrentMission), int(unit->QueuedMission),
		unit->Destination == target, unit->Target == target, int(unit->SequenceAnim));
}
}

void InfantryExt::ResetCaptureDelay()
{
	this->EngineerCaptureTarget = nullptr;
	this->EngineerCaptureOwner = nullptr;
	this->EngineerCaptureTargetOwner = nullptr;
	this->EngineerCaptureTotal = 0;
	this->EngineerCaptureTimer.Stop();
}

void InfantryExt::OnDetach(TechnoClass* target, bool removed)
{
	FootExt::OnDetach(target, removed);
	if (target == this->EngineerCaptureTarget)
		this->ResetCaptureDelay();
}

void InfantryExt::UpdateCaptureDelay()
{
	auto const target = this->EngineerCaptureTarget;
	if (!target)
		return;
	auto const unit = this->OwnerObject();
	// New orders win immediately; never replace a newly issued destination.
	if (unit->CurrentMission != Mission::Capture || (unit->QueuedMission != Mission::None && unit->QueuedMission != Mission::Capture)
		|| (unit->Target && unit->Target != target) || (unit->Destination && unit->Destination != target)
		|| (!unit->Target && !unit->Destination))
	{
		ReportCaptureState(unit, target, "cancel-order", this->EngineerCaptureTimer.GetTimeLeft());
		this->ResetCaptureDelay();
		return;
	}
	if (!unit->IsAlive || unit->Health <= 0 || unit->InLimbo || !unit->Type->Engineer
		|| !target->IsAlive || target->Health <= 0 || target->InLimbo
		|| CaptureDelayFor(this->GetTypeExtData(), target) <= 0
		|| unit->Owner != this->EngineerCaptureOwner || target->Owner != this->EngineerCaptureTargetOwner
		|| unit->Owner->IsAlliedWith(target->Owner) || !unit->Locomotor || unit->Deactivated || unit->IsUnderEMP()
		|| unit->IsImmobilized || unit->TemporalTargetingMe || unit->BeingWarpedOut || FootExt::CannotMove(unit, true))
	{
		ReportCaptureState(unit, target, "cancel-invalid", this->EngineerCaptureTimer.GetTimeLeft());
		this->ResetCaptureDelay();
		if (unit->IsAlive && unit->Health > 0 && !unit->InLimbo)
		{
			unit->SetTarget(nullptr);
			unit->SetDestination(nullptr, false);
			unit->QueueMission(Mission::Guard, true);
		}
		return;
	}
	if (this->EngineerCaptureTimer.Completed())
	{
		// Stop marks this order as released, while keeping the target/completion
		// token. It is serialized already and prevents restarting the countdown.
		this->EngineerCaptureTimer.Stop();
		ReportCaptureState(unit, target, "complete-resume", 0);
		if (unit->SequenceAnim == Sequence::Idle1 || unit->SequenceAnim == Sequence::Idle2
			|| unit->SequenceAnim == Sequence::WetIdle1 || unit->SequenceAnim == Sequence::WetIdle2)
			unit->PlayAnim(Sequence::Ready, true);
		// Re-enter the native infantry destination path once, as the existing
		// temporary-immobilization recovery does. Let it choose the entrance;
		// never replace this with a Move mission or a building-centre Move_To.
		unit->SetDestination(target, false);
	}
}

bool InfantryExt::HoldForCaptureDelay()
{
	this->UpdateCaptureDelay();
	if (this->EngineerCaptureTarget)
		return this->EngineerCaptureTimer.GetTimeLeft() > 0;
	auto const unit = this->OwnerObject();
	if (!unit->Type->Engineer || unit->CurrentMission != Mission::Capture
		|| !unit->Locomotor || unit->Deactivated || unit->IsUnderEMP() || unit->IsImmobilized
		|| unit->TemporalTargetingMe || unit->BeingWarpedOut || FootExt::CannotMove(unit, true))
		return false;
	auto const target = abstract_cast<BuildingClass*>(unit->Destination ? unit->Destination : unit->Target);
	if (!target || target->InLimbo || !target->IsAlive || target->Health <= 0 || unit->Owner->IsAlliedWith(target->Owner)
		|| target->Type->BridgeRepairHut)
		return false;
	const int delay = CaptureDelayFor(this->GetTypeExtData(), target);
	if (delay <= 0)
		return false;
	// Inspect the locomotor's immediate next step, not its remote destination.
	const auto head = unit->Locomotor->Head_To_Coord();
	auto cell = head != CoordStruct::Empty ? MapClass::Instance.TryGetCellAt(head) : nullptr;
	auto const currentCell = unit->GetCell();
	// A freshly assigned path may not yet have a Head_To_Coord. Its first
	// direction is already an immediate movement step, never the remote goal.
	const int direction = unit->PathDirections[0];
	if ((!cell || cell->GetBuilding() != target) && direction >= 0 && direction < 8)
		cell = MapClass::Instance.TryGetCellAt(unit->GetMapCoords() + CellSpread::GetNeighbourOffset(direction));
	if (!cell || cell->GetBuilding() != target || !currentCell || currentCell->GetBuilding() == target)
		return false;
	this->EngineerCaptureTarget = target;
	this->EngineerCaptureOwner = unit->Owner;
	this->EngineerCaptureTargetOwner = target->Owner;
	this->EngineerCaptureTotal = delay;
	this->EngineerCaptureTimer.Start(delay);
	static unsigned reports = 0;
	if (reports < 64)
	{
		++reports;
		const auto building = BuildingTypeExt::Fetch(target->Type);
		Debug::Log("EngineerCapture: revision=5 start unit=%u target=%u delay=%d source=%s.\n", unit->UniqueID, target->UniqueID, delay,
			building && building->Engineer_CaptureDelay.isset() ? "building" : "engineer");
	}
	ReportCaptureState(unit, target, "start", delay);
	return true;
}

bool InfantryExt::PlayCaptureIdle()
{
	if (!this->EngineerCaptureTarget || this->EngineerCaptureTimer.GetTimeLeft() <= 0)
		return false;
	auto const unit = this->OwnerObject();
	auto const sequences = unit->Type->Sequence;
	if (!sequences)
		return true;
	// Keep an idle animation running rather than resetting its frame every tick.
	if (unit->SequenceAnim != Sequence::Idle1 && unit->SequenceAnim != Sequence::Idle2
		&& unit->SequenceAnim != Sequence::WetIdle1 && unit->SequenceAnim != Sequence::WetIdle2)
	{
		const auto idle = sequences->GetSequence(Sequence::Idle1).CountFrames > 0 ? Sequence::Idle1
			: sequences->GetSequence(Sequence::Idle2).CountFrames > 0 ? Sequence::Idle2 : Sequence::Ready;
		// Native PlayAnim handles amphibious/wet sequence mapping. Force is needed
		// to leave an uninterruptible Walk; same-sequence calls do not restart it.
		unit->PlayAnim(idle, true);
	}
	return true;
}

void InfantryExt::DrawCaptureProgress(RectangleStruct* bounds)
{
	auto const unit = this->OwnerObject();
	if (!this->EngineerCaptureTarget || this->EngineerCaptureTotal <= 0 || !HouseClass::CurrentPlayer
		|| !unit->Owner->IsAlliedWith(HouseClass::CurrentPlayer) || unit->InLimbo || unit->Health <= 0)
		return;
	auto const cell = unit->GetCell();
	if (!cell || cell->IsFogged() || cell->IsShrouded())
		return;
	auto const typeExt = this->GetTypeExtData();
	auto point = TechnoExt::GetFootSelectBracketPosition(unit, Anchor(HorizontalPosition::Center, VerticalPosition::Top), true)
		+ Point2D { 0, -6 } + typeExt->Engineer_CaptureProgress_Offset.Get();
	const int remaining = std::clamp(this->EngineerCaptureTimer.GetTimeLeft(), 0, this->EngineerCaptureTotal);
	const int elapsed = this->EngineerCaptureTotal - remaining;
	auto const surface = DSurface::Composite;
	if (auto const reference = typeExt->Engineer_CaptureProgress_SHP.Get())
	{
		auto const shape = reference->GetData();
		if (shape && shape->Frames > 0)
		{
			const int frame = static_cast<int>(1LL * elapsed * (shape->Frames - 1) / this->EngineerCaptureTotal);
			auto const palette = typeExt->Engineer_CaptureProgress_Palette.GetOrDefaultConvert(FileSystem::ANIM_PAL);
			if (palette)
			{
				surface->DrawSHP(palette, shape, frame, &point, bounds, BlitterFlags::Centered | BlitterFlags::bf_400,
					0, 0, ZGradient::Ground, 1000, 0, 0, 0, 0, 0);
				return;
			}
		}
	}
	RectangleStruct outer { point.X - 16, point.Y - 2, 32, 5 };
	RectangleStruct background { outer.X + 1, outer.Y + 1, 30, 3 };
	const int width = static_cast<int>(30LL * elapsed / this->EngineerCaptureTotal);
	RectangleStruct fill { background.X, background.Y, width, background.Height };
	surface->FillRectEx(bounds, &background, 0);
	surface->DrawRectEx(bounds, &outer, Drawing::RGB_To_Int(ColorStruct { 80, 150, 170 }));
	if (width > 0)
		surface->FillRectEx(bounds, &fill, Drawing::RGB_To_Int(ColorStruct { 80, 230, 170 }));
}

// Returns hardcoded prone/deployed FLH overrides for infantry, if set.
CoordStruct InfantryExt::GetSimpleFLH(InfantryClass* pThis, int weaponIndex, bool& FLHFound)
{
	FLHFound = false;
	CoordStruct FLH = CoordStruct::Empty;

	auto const pTypeExt = InfantryTypeExt::Fetch(pThis->Type);
	Nullable<CoordStruct> pickedFLH;

	if (pThis->IsDeployed())
	{
		if (weaponIndex == 0)
			pickedFLH = pTypeExt->DeployedPrimaryFireFLH;
		else if (weaponIndex == 1)
			pickedFLH = pTypeExt->DeployedSecondaryFireFLH;
	}
	else
	{
		if (pThis->Crawling)
		{
			if (weaponIndex == 0)
				pickedFLH = pTypeExt->PronePrimaryFireFLH;
			else if (weaponIndex == 1)
				pickedFLH = pTypeExt->ProneSecondaryFireFLH;
		}
	}

	if (pickedFLH.isset())
	{
		FLH = pickedFLH.Get();
		FLHFound = true;
	}

	return FLH;
}

// =============================
// load / save

template <typename T>
void InfantryExt::Serialize(T& Stm)
{
	Stm
		.Process(this->SkipTargetChangeResetSequence)
		.Process(this->HasDeployConverted)
		.Process(this->HasUndeployConverted)
		.Process(this->RestartReloadTimer)
		.Process(this->EngineerCaptureTarget)
		.Process(this->EngineerCaptureOwner)
		.Process(this->EngineerCaptureTargetOwner)
		.Process(this->EngineerCaptureTimer)
		.Process(this->EngineerCaptureTotal)
		;
}

void InfantryExt::LoadFromStream(PhobosStreamReader& Stm)
{
	FootExt::LoadFromStream(Stm);
	this->Serialize(Stm);
}

void InfantryExt::SaveToStream(PhobosStreamWriter& Stm)
{
	FootExt::SaveToStream(Stm);
	this->Serialize(Stm);
}

// =============================
// container

InfantryExt::ExtContainer::ExtContainer() : Container("InfantryClass") { }
InfantryExt::ExtContainer::~ExtContainer() = default;

// =============================
// container hooks

DEFINE_HOOK(0x517A60, InfantryClass_CTOR, 0xE)
{
	GET(InfantryClass*, pItem, ESI);

	InfantryExt::ExtMap.Allocate(pItem);

	return 0;
}

// Late in every destructor body of the class, right before it chains into the
// base destructor: the last point where the extension is no longer used.
DEFINE_HOOK(0x517F81, InfantryClass_DTOR, 0x8)
{
	GET(InfantryClass*, pItem, ESI);

	InfantryExt::ExtMap.Remove(pItem);

	return 0;
}
