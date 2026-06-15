// Copyright 2014-2017 Oxford University Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../Scene/ITMVoxelBlockHash.h"
#include "../../../ORUtils/Image.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>

namespace ITMLib
{
	class ITMMesh
	{
	public:
		struct Triangle { Vector3f p0, p1, p2; };

		MemoryDeviceType memoryType;

		uint noTotalTriangles;
		static const uint noMaxTriangles_default = SDF_LOCAL_BLOCK_NUM * 32 * 16;
		uint noMaxTriangles;

		ORUtils::MemoryBlock<Triangle> *triangles;

		explicit ITMMesh(MemoryDeviceType memoryType, uint maxTriangles = noMaxTriangles_default)
		{
			this->memoryType = memoryType;
			this->noTotalTriangles = 0;
			this->noMaxTriangles = maxTriangles;

			triangles = new ORUtils::MemoryBlock<Triangle>(noMaxTriangles, memoryType);
		}

		void WriteOBJ(const char *fileName)
		{
			ORUtils::MemoryBlock<Triangle> *cpu_triangles; bool shoulDelete = false;
			if (memoryType == MEMORYDEVICE_CUDA)
			{
				cpu_triangles = new ORUtils::MemoryBlock<Triangle>(noMaxTriangles, MEMORYDEVICE_CPU);
				cpu_triangles->SetFrom(triangles, ORUtils::MemoryBlock<Triangle>::CUDA_TO_CPU);
				shoulDelete = true;
			}
			else cpu_triangles = triangles;

			Triangle *triangleArray = cpu_triangles->GetData(MEMORYDEVICE_CPU);

			// Vertex welding: Marching Cubes produces the same exact float bits for vertices
			// shared between adjacent cubes — bitwise hashing is sufficient to merge them.
			// Without this, each triangle gets 3 unique vertices (triangle soup).
			struct Vec3Key {
				uint32_t x, y, z;
				bool operator==(const Vec3Key& o) const { return x==o.x && y==o.y && z==o.z; }
			};
			struct Vec3KeyHash {
				size_t operator()(const Vec3Key& k) const {
					// FNV-1a 32-bit on three words
					uint32_t h = 2166136261u;
					h = (h ^ k.x) * 16777619u;
					h = (h ^ k.y) * 16777619u;
					h = (h ^ k.z) * 16777619u;
					return static_cast<size_t>(h);
				}
			};

			std::unordered_map<Vec3Key, uint32_t, Vec3KeyHash> vertexMap;
			std::vector<Vector3f> uniqueVertices;
			std::vector<std::array<uint32_t, 3>> faces;

			vertexMap.reserve(noTotalTriangles);
			uniqueVertices.reserve(noTotalTriangles / 2 + 1);
			faces.reserve(noTotalTriangles);

			auto getOrAdd = [&](const Vector3f& p) -> uint32_t {
				Vec3Key key;
				std::memcpy(&key.x, &p.x, sizeof(uint32_t));
				std::memcpy(&key.y, &p.y, sizeof(uint32_t));
				std::memcpy(&key.z, &p.z, sizeof(uint32_t));
				auto it = vertexMap.find(key);
				if (it != vertexMap.end()) return it->second;
				uint32_t idx = static_cast<uint32_t>(uniqueVertices.size());
				vertexMap.emplace(key, idx);
				uniqueVertices.push_back(p);
				return idx;
			};

			for (uint i = 0; i < noTotalTriangles; i++)
			{
				uint32_t i0 = getOrAdd(triangleArray[i].p0);
				uint32_t i1 = getOrAdd(triangleArray[i].p1);
				uint32_t i2 = getOrAdd(triangleArray[i].p2);
				faces.push_back({{i0, i1, i2}});
			}

			FILE *f = fopen(fileName, "w+");
			if (f != NULL)
			{
				for (const Vector3f& v : uniqueVertices)
					fprintf(f, "v %f %f %f\n", v.x, v.y, v.z);

				// Preserve original winding order (p2, p1, p0 — same as before).
				for (const auto& face : faces)
					fprintf(f, "f %u %u %u\n", face[2]+1, face[1]+1, face[0]+1);

				fclose(f);
			}

			if (shoulDelete) delete cpu_triangles;
		}

		void WriteSTL(const char *fileName)
		{
			ORUtils::MemoryBlock<Triangle> *cpu_triangles; bool shoulDelete = false;
			if (memoryType == MEMORYDEVICE_CUDA)
			{
				cpu_triangles = new ORUtils::MemoryBlock<Triangle>(noMaxTriangles, MEMORYDEVICE_CPU);
				cpu_triangles->SetFrom(triangles, ORUtils::MemoryBlock<Triangle>::CUDA_TO_CPU);
				shoulDelete = true;
			}
			else cpu_triangles = triangles;

			Triangle *triangleArray = cpu_triangles->GetData(MEMORYDEVICE_CPU);

			FILE *f = fopen(fileName, "wb+");

			if (f != NULL) {
				for (int i = 0; i < 80; i++) fwrite(" ", sizeof(char), 1, f);

				fwrite(&noTotalTriangles, sizeof(int), 1, f);

				float zero = 0.0f; short attribute = 0;
				for (uint i = 0; i < noTotalTriangles; i++)
				{
					fwrite(&zero, sizeof(float), 1, f); fwrite(&zero, sizeof(float), 1, f); fwrite(&zero, sizeof(float), 1, f);

					fwrite(&triangleArray[i].p2.x, sizeof(float), 1, f); 
					fwrite(&triangleArray[i].p2.y, sizeof(float), 1, f); 
					fwrite(&triangleArray[i].p2.z, sizeof(float), 1, f);

					fwrite(&triangleArray[i].p1.x, sizeof(float), 1, f); 
					fwrite(&triangleArray[i].p1.y, sizeof(float), 1, f); 
					fwrite(&triangleArray[i].p1.z, sizeof(float), 1, f);

					fwrite(&triangleArray[i].p0.x, sizeof(float), 1, f);
					fwrite(&triangleArray[i].p0.y, sizeof(float), 1, f);
					fwrite(&triangleArray[i].p0.z, sizeof(float), 1, f);

					fwrite(&attribute, sizeof(short), 1, f);

					//fprintf(f, "v %f %f %f\n", triangleArray[i].p0.x, triangleArray[i].p0.y, triangleArray[i].p0.z);
					//fprintf(f, "v %f %f %f\n", triangleArray[i].p1.x, triangleArray[i].p1.y, triangleArray[i].p1.z);
					//fprintf(f, "v %f %f %f\n", triangleArray[i].p2.x, triangleArray[i].p2.y, triangleArray[i].p2.z);
				}

				//for (uint i = 0; i<noTotalTriangles; i++) fprintf(f, "f %d %d %d\n", i * 3 + 2 + 1, i * 3 + 1 + 1, i * 3 + 0 + 1);
				fclose(f);
			}

			if (shoulDelete) delete cpu_triangles;
		}

		void Write(const char *fileName)
		{
			const char *extension = strrchr(fileName, '.');
			if ((extension != NULL) && ((strcmp(extension, ".obj") == 0) || (strcmp(extension, ".OBJ") == 0)))
				WriteOBJ(fileName);
			else
				WriteSTL(fileName);
		}

		~ITMMesh()
		{
			delete triangles;
		}

		// Suppress the default copy constructor and assignment operator
		ITMMesh(const ITMMesh&);
		ITMMesh& operator=(const ITMMesh&);
	};
}
