#pragma once
#include "Math/Transform.h"
#include "Mesh.h"
#include "Shader.h"
#include "Material.h"
#include <array>
#include <utility>

class FPrimitiveSceneProxy;
inline constexpr uint32 InvalidObjectSlot = ~0u;

struct FRenderPacket
{
	const FPrimitiveSceneProxy* Proxy = nullptr;
	const FMatrix* Model = nullptr;
	const FVertexFactory* VertexFactory = nullptr;
	FIndexBuffer* IndexBuffer = nullptr;
	UMaterial* Material = nullptr;
	const void* MaterialParamData = nullptr;
	float CameraToParticleDistance = 0.0f;
	uint32 MaterialParamDataSize = 0;
	uint32 StartIndex = 0;
	uint32 IndexCount = 0; // Zero selects the full index buffer.
	uint32 Slot = InvalidObjectSlot;
	uint8 LODIndex = 0;
	bool bOccludedByGpu = false;
};

// Shared bindings, with one ordinary indexed draw per item.
struct FStaticDrawItem
{
	const FPrimitiveSceneProxy* Proxy;
	uint32 Slot;
	uint32 StartIndex;
	uint32 IndexCount;
	uint32 bOccludedByGpu;
};

struct FStaticDrawGroup
{
	UMaterial* Material = nullptr;
	UStaticMesh* Mesh = nullptr;
	uint8 LODIndex = 0;
	std::vector<FStaticDrawItem> Items;
};

// Owns the current frame's billboard/particle matrices together with their packets.
// Blocks keep matrix addresses stable when either array grows.
class FRenderQueue
{
	static constexpr uint32 MatricesPerBlock = 256;
	using FMatrixBlock = std::array<FMatrix, MatricesPerBlock>;
	// 패킷 배열을 상속하지 않고 가진다. 배열 함수로 비우면 행렬 개수가 남는 실수를 막는다.
	TArray<FRenderPacket> Packets;
	TArray<TUniquePtr<FMatrixBlock>> MatrixBlocks;
	uint32 MatrixCount = 0;

public:
	FRenderQueue() = default;
	FRenderQueue(const FRenderQueue&) = delete;
	FRenderQueue& operator=(const FRenderQueue&) = delete;
	FRenderQueue(FRenderQueue&& Other) noexcept
		: Packets(std::move(Other.Packets))
		, MatrixBlocks(std::move(Other.MatrixBlocks))
		, MatrixCount(std::exchange(Other.MatrixCount, 0))
	{
	}
	FRenderQueue& operator=(FRenderQueue&& Other) noexcept
	{
		if (this != &Other)
		{
			Packets = std::move(Other.Packets);
			MatrixBlocks = std::move(Other.MatrixBlocks);
			MatrixCount = std::exchange(Other.MatrixCount, 0);
		}
		return *this;
	}
	const FMatrix* StoreWorldMatrix(const FMatrix& World)
	{
		const uint32 BlockIndex = MatrixCount / MatricesPerBlock;
		if (BlockIndex >= static_cast<uint32>(MatrixBlocks.Num()))
		{
			MatrixBlocks.Add(MakeUnique<FMatrixBlock>());
		}
		FMatrix& Stored = (*MatrixBlocks[BlockIndex])[MatrixCount % MatricesPerBlock];
		Stored = World;
		++MatrixCount;
		return &Stored;
	}
	// 패킷과 이번 프레임의 행렬을 함께 비운다. 블록 메모리는 재사용한다.
	void Reset()
	{
		Packets.Reset();
		MatrixCount = 0;
	}

	TArray<FRenderPacket>& GetPackets() { return Packets; }
	const TArray<FRenderPacket>& GetPackets() const { return Packets; }
	void Add(const FRenderPacket& Packet) { Packets.Add(Packet); }
	[[nodiscard]] FRenderPacket& AddDefaulted_GetRef() { return Packets.AddDefaulted_GetRef(); }
	void Append(const TArray<FRenderPacket>& InPackets) { Packets.Append(InPackets); }
	void Reserve(int32 Count) { Packets.Reserve(Count); }
	int32 Num() const { return Packets.Num(); }
	size_t size() const { return Packets.size(); }
	bool IsEmpty() const { return Packets.Num() == 0; }
	FRenderPacket& operator[](int32 Index) { return Packets[Index]; }
	const FRenderPacket& operator[](int32 Index) const { return Packets[Index]; }
	auto begin() { return Packets.begin(); }
	auto end() { return Packets.end(); }
	auto begin() const { return Packets.begin(); }
	auto end() const { return Packets.end(); }
};
