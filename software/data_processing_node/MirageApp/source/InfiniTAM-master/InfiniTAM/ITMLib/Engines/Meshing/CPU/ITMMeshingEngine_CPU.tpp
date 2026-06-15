// Copyright 2014-2017 Oxford University Innovation Limited and the authors of InfiniTAM

#include "ITMMeshingEngine_CPU.h"
#include "../Shared/ITMMeshingEngine_Shared.h"

#include <vector>
#include <cstring>

using namespace ITMLib;

template<class TVoxel>
void ITMMeshingEngine_CPU<TVoxel, ITMVoxelBlockHash>::MeshScene(ITMMesh *mesh, const ITMScene<TVoxel, ITMVoxelBlockHash> *scene)
{
	const TVoxel *localVBA = scene->localVBA.GetVoxelBlocks();
	const ITMHashEntry *hashTable = scene->index.GetEntries();
	int noTotalEntries = scene->index.noTotalEntries;
	float factor = scene->sceneParams->voxelSize;

	// Collect into a vector to avoid the ~4.8GB pre-allocated buffer.
	// The final mesh buffer is sized to exactly what's needed.
	std::vector<ITMMesh::Triangle> triBuffer;
	triBuffer.reserve(65536);

	for (int entryId = 0; entryId < noTotalEntries; entryId++)
	{
		const ITMHashEntry &currentHashEntry = hashTable[entryId];
		if (currentHashEntry.ptr < 0) continue;

		Vector3i globalPos = currentHashEntry.pos.toInt() * SDF_BLOCK_SIZE;

		for (int z = 0; z < SDF_BLOCK_SIZE; z++) for (int y = 0; y < SDF_BLOCK_SIZE; y++) for (int x = 0; x < SDF_BLOCK_SIZE; x++)
		{
			Vector3f vertList[12];
			int cubeIndex = buildVertList(vertList, globalPos, Vector3i(x, y, z), localVBA, hashTable);
			if (cubeIndex < 0) continue;

			for (int i = 0; triangleTable[cubeIndex][i] != -1; i += 3)
			{
				ITMMesh::Triangle tri;
				tri.p0 = vertList[triangleTable[cubeIndex][i    ]] * factor;
				tri.p1 = vertList[triangleTable[cubeIndex][i + 1]] * factor;
				tri.p2 = vertList[triangleTable[cubeIndex][i + 2]] * factor;
				triBuffer.push_back(tri);
			}
		}
	}

	const uint noTriangles = (uint)triBuffer.size();

	// Resize the mesh's triangle buffer to the exact count if the pre-allocated block is too small.
	if (noTriangles > mesh->noMaxTriangles)
	{
		delete mesh->triangles;
		mesh->triangles = new ORUtils::MemoryBlock<ITMMesh::Triangle>(noTriangles, MEMORYDEVICE_CPU);
		mesh->noMaxTriangles = noTriangles;
	}

	mesh->noTotalTriangles = noTriangles;
	if (noTriangles > 0)
		memcpy(mesh->triangles->GetData(MEMORYDEVICE_CPU), triBuffer.data(), noTriangles * sizeof(ITMMesh::Triangle));
}