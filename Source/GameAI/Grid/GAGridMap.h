#pragma once

#include "CoreMinimal.h"
#include "Math/MathFwd.h"
#include "GAGridMap.generated.h"


// This allows us to define a set of floating point values over the grid defined by a AGAGridActor.
// Note that it does not necessarily need to cover the entire grid

class AGAGridActor;
struct FCellRef;

USTRUCT(BlueprintType)
struct FGridBox
{
	GENERATED_USTRUCT_BODY()

	FGridBox() : MinX(INDEX_NONE), MaxX(INDEX_NONE), MinY(INDEX_NONE), MaxY(INDEX_NONE) {}
	FGridBox(int32 MinXIn, int32 MaxXIn, int32 MinYIn, int32 MaxYIn) : MinX(MinXIn), MaxX(MaxXIn), MinY(MinYIn), MaxY(MaxYIn) {}
	FGridBox(const FIntRect& Rect)
	{
		MinX = Rect.Min.X;
		MaxX = Rect.Max.X;
		MinY = Rect.Min.Y;
		MaxY = Rect.Max.Y;
	}

	UPROPERTY(BlueprintReadWrite)
	int32 MinX;

	UPROPERTY(BlueprintReadWrite)
	int32 MaxX;

	UPROPERTY(BlueprintReadWrite)
	int32 MinY;

	UPROPERTY(BlueprintReadWrite)
	int32 MaxY;

	bool IsValid() const
	{
		return (MinX != INDEX_NONE) && (MaxX != INDEX_NONE) && (MinY != INDEX_NONE) && (MaxY != INDEX_NONE) && (MinX <= MaxX) && (MinY <= MaxY);
	}

	int32 GetWidth() const { return (MaxX - MinX) + 1; }
	int32 GetHeight() const { return (MaxY - MinY) + 1; }
	int32 GetCellCount() const { return ((MaxX - MinX) + 1) * ((MaxY - MinY) + 1); }

	bool IsValidCell(const FCellRef& Cell) const;
};


USTRUCT(BlueprintType)
struct FGAGridMap
{
	GENERATED_USTRUCT_BODY()

	FGAGridMap();
	FGAGridMap(int32 XCountIn, int32 YCountIn, float InitialValue);
	FGAGridMap(const AGAGridActor *Grid, float InitialValue);
	FGAGridMap(const AGAGridActor* Grid, const FGridBox &GridBoxIn, float InitialValue);

	void ResetData(float InitialValue);

	// The XCount of the GridActor I'm built on
	UPROPERTY(BlueprintReadOnly)
	int32 XCount;

	// The YCount of the GridActor I'm built on
	UPROPERTY(BlueprintReadOnly)
	int32 YCount;

	// Annoyingly, FIntRect is not exposable to blueprint. So we will hold onto the
	// underlying bounds and construct one on demand

	// The bounds over which I am defined
	UPROPERTY(BlueprintReadOnly)
	FGridBox GridBounds;

	UPROPERTY(BlueprintReadOnly)
	TArray<float> Data;

	// Get map-local X,Y coordinates from a cell reference. Remember that the map can be a sub-region of the grid
	// So the resulting local X and Y are used to index into the Data array
	bool CellRefToLocal(const FCellRef& Cell, int32& X, int32& Y) const;

	// Get a cell reference (which can be used to interact with the grid actor) from map-local X,Y coordinates. 
	// Remember that the map can be a sub-region of the grid
	bool LocalToCellRef(int32 X, int32 Y, FCellRef& Cell) const;

	// Get the value that this map stores for the given grid cell
	bool GetValue(const FCellRef& Cell, float& ValueOut) const;

	// Get the maximum value stored in this map
	// Ignore any values great than or equal to the IgnoreThreshold
	bool GetMaxValue(float& MaxValueOut, float IgnoreThreshold = FLT_MAX) const;

	// Set the value that this map stores for the given grid cell
	bool SetValue(const FCellRef& Cell, float Value);


	FORCEINLINE bool IsValid() const
	{
		return GridBounds.IsValid() && (GridBounds.GetCellCount() == Data.Num());
	}
};