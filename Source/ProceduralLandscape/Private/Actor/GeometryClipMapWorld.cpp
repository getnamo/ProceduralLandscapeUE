//Copyright Maxime Dupart 2021  https://twitter.com/Max_Dupt


#include "Actor/GeometryClipMapWorld.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Materials/MaterialParameterCollection.h"
#include "DrawDebugHelpers.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Async/ParallelFor.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/AutoDestroySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Component/GeoClipmapMeshComponent.h"


// Sets default values
AGeometryClipMapWorld::AGeometryClipMapWorld(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("Root"));

}

// Called when the game starts or when spawned
void AGeometryClipMapWorld::BeginPlay()
{
	Super::BeginPlay();
}

void AGeometryClipMapWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for(FCollisionMeshElement& el : CollisionMesh)
	{
		if(el.CollisionRT->IsRooted())
			el.CollisionRT->RemoveFromRoot();
		if (el.Mesh && el.Mesh->IsRooted())
			el.Mesh->RemoveFromRoot();
	}
	for (FClipMapMeshElement& el : Meshes)
	{
		if (el.HeightMap && el.HeightMap->IsRooted())
			el.HeightMap->RemoveFromRoot();

		if (el.NormalMap && el.NormalMap->IsRooted())
			el.NormalMap->RemoveFromRoot();

		if (el.Mesh && el.Mesh->IsRooted())
			el.Mesh->RemoveFromRoot();

		if (el.I_Mesh && el.I_Mesh->IsRooted())
			el.I_Mesh->RemoveFromRoot();

		if (el.MatDyn && el.MatDyn->IsRooted())
			el.MatDyn->RemoveFromRoot();
		if (el.CacheMatDyn && el.CacheMatDyn->IsRooted())
			el.CacheMatDyn->RemoveFromRoot();
		

		for (UTextureRenderTarget2D* el_rt : el.LandLayers)
		{
			if (el_rt && el_rt->IsRooted())
				el_rt->RemoveFromRoot();
		}
		for (UMaterialInstanceDynamic* el_DM : el.LayerMatDyn)
		{
			if (el_DM && el_DM->IsRooted())
				el_DM->RemoveFromRoot();
		}
	}
	for (FSpawnableMesh& el : Spawnables)
	{
		el.CleanUp();
	}
	

	Super::EndPlay(EndPlayReason);
}
#if WITH_EDITOR
bool AGeometryClipMapWorld::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

const TCHAR* EnumToString(EClipMapInteriorConfig InCurrentState)
{
	switch (InCurrentState)
	{
	case EClipMapInteriorConfig::BotLeft:
		return TEXT("BotLeft");
	case EClipMapInteriorConfig::BotRight:
		return TEXT("BotRight");
	case EClipMapInteriorConfig::TopLeft:
		return TEXT("TopLeft");
	case EClipMapInteriorConfig::TopRight:
		return TEXT("TopRight");
	case EClipMapInteriorConfig::NotVisible:
		return TEXT("NotVisible");
	}
	ensure(false);
	return TEXT("Unknown");
}

void AGeometryClipMapWorld::Setup()
{
	SetN();
	TimeAcu = 0.f;
	DrawCall_Spawnables_count = 0;

	UpdateViewFrustum();

	UpdateCameraLocation();

	if (GenerateCollision_last != GenerateCollision || VerticalRangeMeters_last != VerticalRangeMeters || Caching_last != EnableCaching)
		rebuild = true;


	if (rebuild)
	{
		for (int i = Meshes.Num() - 1; i >= 0; i--)
		{
			FClipMapMeshElement& Elem = Meshes[i];

			if (Elem.HeightMap && Elem.HeightMap->IsRooted())
					Elem.HeightMap->RemoveFromRoot();
			if (Elem.NormalMap && Elem.NormalMap->IsRooted())
					Elem.NormalMap->RemoveFromRoot();

			if (Elem.MatDyn && Elem.MatDyn->IsRooted())
				Elem.MatDyn->RemoveFromRoot();			
			if (Elem.CacheMatDyn && Elem.CacheMatDyn->IsRooted())
				Elem.CacheMatDyn->RemoveFromRoot();			
			

			if (Elem.Mesh)
			{
				if (Elem.Mesh->IsRooted())
					Elem.Mesh->RemoveFromRoot();
				
				Elem.Mesh->ClearAllMeshSections();
				Elem.Mesh->UnregisterComponent();
				Elem.Mesh->DestroyComponent();
				Elem.Mesh = nullptr;				
			}
			if (Elem.I_Mesh)
			{
				if (Elem.I_Mesh->IsRooted())
					Elem.I_Mesh->RemoveFromRoot();

				Elem.I_Mesh->ClearInstances();
				Elem.I_Mesh->UnregisterComponent();
				Elem.I_Mesh->DestroyComponent();
				Elem.I_Mesh = nullptr;				
			}
			Elem.HeightMap = nullptr;
			Elem.NormalMap = nullptr;

			for (UTextureRenderTarget2D* el : Elem.LandLayers)
			{
				if (el && el->IsRooted())
					el->RemoveFromRoot();
			}

			for (UMaterialInstanceDynamic* el_DM : Elem.LayerMatDyn)
			{
				if (el_DM && el_DM->IsRooted())
					el_DM->RemoveFromRoot();
			}

			Elem.LandLayers.Empty();
			Elem.LayerMatDyn.Empty();
			Elem.LandLayers_names.Empty();
		}

		Meshes.Empty();

		for (int i = CollisionMesh.Num() - 1; i >= 0; i--)
		{
			FCollisionMeshElement& Elem = CollisionMesh[i];
			if (Elem.Mesh)
			{
				if (Elem.Mesh->IsRooted())
					Elem.Mesh->RemoveFromRoot();

				if (Elem.CollisionRT->IsRooted())
					Elem.CollisionRT->RemoveFromRoot();

				Elem.Mesh->ClearAllMeshSections();
				Elem.Mesh->UnregisterComponent();
				Elem.Mesh->DestroyComponent();
				Elem.Mesh = nullptr;
				Elem.CollisionRT = nullptr;
			}

		}

		CollisionMesh.Empty();
		AvailableCollisionMesh.Empty();
		UsedCollisionMesh.Empty();
		GroundCollisionLayout.Empty();

		for (FSpawnableMesh& Spawnable : Spawnables)
		{
			Spawnable.CleanUp();
		}
		SortedSpawnables.Empty();
		

		rebuild = false;
		GenerateCollision_last = GenerateCollision;
		VerticalRangeMeters_last = VerticalRangeMeters;
		Caching_last = EnableCaching;
	}

	if(rebuildVegetationOnly)
	{
		for (FSpawnableMesh& Spawnable : Spawnables)
		{
			Spawnable.CleanUp();
		}
		SortedSpawnables.Empty();
		rebuildVegetationOnly=false;
	}
}

// Called every frame
void AGeometryClipMapWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	
	if(RTUpdate.IsFenceComplete())
	{
		ProcessCollisionsPending();

		ProcessSpawnablePending();		
	}

	TimeAcu+=DeltaTime;
	
	if(!(TimeAcu>1.0/(FMath::Clamp(UpdateRatePerSecond,1.f,200.f))) || !RTUpdate.IsFenceComplete())
		return;

	Setup();

	if (GenerateCollision )
		UpdateCollisionMesh();

	if(Meshes.Num()==0)
		InitiateWorld();
	

	UpdateClipMap();
	UpdateSpawnables();


	RTUpdate.BeginFence();
}

FVector AGeometryClipMapWorld::Get_LOD_RingLocation(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{		
		return Meshes[Meshes.Num() - 1 - LOD].Location;
	}
	return FVector();
}

UTextureRenderTarget2D* AGeometryClipMapWorld::Get_LOD_HeightMap(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].HeightMap;
	}
	return nullptr;
}

UTextureRenderTarget2D* AGeometryClipMapWorld::Get_LOD_NormalMap(int LOD)
{
	if (Meshes.Num() > 0 && LOD >= 0 && LOD < Meshes.Num())
	{
		return Meshes[Meshes.Num() - 1 - LOD].NormalMap;
	}
	return nullptr;
}

void AGeometryClipMapWorld::UpdateStaticDataFor(AGeometryClipMapWorld* Source_)
{
	if(!Source_ || Source_!=DataSource || Source_ && Source_->GetMeshNum()==0 || Source_ && !Source_->EnableCaching)
		return;

	int SourceMaxLOD = Source_->LOD_Num-1;

	for(FClipMapMeshElement& el : Meshes)
	{
		int el_LOD = LOD_Num-1-el.Level;

		if (el.MatDyn)
		{
			int SourceLOD_tolookFor = el_LOD + LOD_Offset_FromReceiverToSource > SourceMaxLOD ? SourceMaxLOD : el_LOD+LOD_Offset_FromReceiverToSource;

			for(int i=SourceLOD_tolookFor; i < Source_->LOD_Num;i++)
			{
			
				if(Source_->LOD_Num-1 - i < Source_->GetMeshNum())
				{
					FClipMapMeshElement& ClipMMesh = Source_->GetMesh(Source_->LOD_Num-1 - i);

					if(ClipMMesh.Mesh->IsMeshSectionVisible(0)||ClipMMesh.Mesh->IsMeshSectionVisible(1))
					{
						int CacheResExt = ClipMMesh.HeightMap->SizeX;

						if(el.HeightMapFromLastSourceElement && el.HeightMapFromLastSourceElement==ClipMMesh.HeightMap)
						{
							el.MatDyn->SetVectorParameterValue("Ext_RingLocation", ClipMMesh.Location);
						}
						else
						{
							el.MatDyn->SetVectorParameterValue("Ext_RingLocation", ClipMMesh.Location);
							el.MatDyn->SetScalarParameterValue("Ext_MeshScale", (Source_->N - 1) * ClipMMesh.GridSpacing * CacheResExt / (CacheResExt - 1));
							el.MatDyn->SetScalarParameterValue("Ext_N", Source_->N);
							el.MatDyn->SetScalarParameterValue("Ext_LocalGridScaling", ClipMMesh.GridSpacing);
							el.MatDyn->SetScalarParameterValue("Ext_CacheRes", CacheResExt);

							el.MatDyn->SetTextureParameterValue("Ext_HeightMap", ClipMMesh.HeightMap);
							el.MatDyn->SetTextureParameterValue("Ext_NormalMap", ClipMMesh.NormalMap);
						}
						

						break;
					}

					
				}

			}
		
		}
	}
}

void AGeometryClipMapWorld::ReceiveExternalDataUpdate(AGeometryClipMapWorld* Source, int LOD_, FVector NewLocation)
{
	if(Source && DataSource && Source==DataSource && Meshes.Num()>0)
	{
		int SourceMaxLOD = Source->LOD_Num-1;

		for (FClipMapMeshElement& el : Meshes)
		{
			int el_LOD = LOD_Num - 1 - el.Level;

			if (el.MatDyn)
			{
				int SourceLOD_tolookFor = el_LOD + LOD_Offset_FromReceiverToSource > SourceMaxLOD ? SourceMaxLOD : el_LOD + LOD_Offset_FromReceiverToSource;

				if(SourceLOD_tolookFor == LOD_)
				{
					el.MatDyn->SetVectorParameterValue("Ext_RingLocation", NewLocation);				
				}

			}
		}
	}
}

#if WITH_EDITOR
void AGeometryClipMapWorld::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* Property = PropertyChangedEvent.MemberProperty;

	if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		FString PropName = PropertyChangedEvent.Property->GetName();

		if(PropName == TEXT("Spawnables") || 
			PropName == TEXT("RegionWorldDimension")|| 
			PropName == TEXT("NumberOfInstanceToComputePerRegion")|| 
			PropName == TEXT("AlignMaxAngle")|| 
			PropName == TEXT("SpawnType")||
			PropName == TEXT("Mesh")||
			PropName == TEXT("Actors")|| 
			PropName == TEXT("AltitudeRange")|| 
			PropName == TEXT("ScaleRange")|| 
			PropName == TEXT("GroundSlopeAngle")||
			PropName == TEXT("NumberRegionPerQuadrantSide"))			
		{			
			rebuildVegetationOnly=true;			
		}
		else if(PropName == TEXT("VerticePerPatch") || 
			PropName == TEXT("LOD_Num")|| 
			PropName == TEXT("ClipMapCacheIntraVerticesTexel")|| 
			PropName == TEXT("WorldDimensionMeters")|| 
			PropName == TEXT("WorldPresentation")|| 
			PropName == TEXT("LandDataLayers") ||
			PropName == TEXT("CollisionMeshVerticeNumber") ||
			PropName == TEXT("LOD_above_doubleCacheResolution") ||
			PropName == TEXT("CollisionMeshPerQuadrantAroundPlayer"))
		{
			rebuild=true;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AGeometryClipMapWorld::SetN()
{
	int N_values[6] = {511,255,127,63,31,15};

	N = N_values[(uint8)VerticePerPatch];
}

bool AGeometryClipMapWorld::CanUpdateSpawnables()
{
	if(DrawCall_Spawnables_count<DrawCallBudget_Spawnables)
	{
		DrawCall_Spawnables_count++;
		return true;
	}
	return false;
}

void AGeometryClipMapWorld::CreateGridMeshWelded(int LOD, int32 NumX, int32 NumY, TArray<int32>& Triangles, TArray<FVector>& Vertices, TArray<FVector2D>& UVs,TArray<FVector2D>& UV1s,TArray<FVector2D>& UV2s, float& GridSpacing_,FVector& Offset, uint8 StitchProfil)
{
	bool StitchX0 = (((StitchProfil>>3) & 1) > 0);
	bool StitchXN = (((StitchProfil>>2) & 1) > 0);

	bool StitchY0 = (((StitchProfil >> 1) & 1) > 0);
	bool StitchYN = ((StitchProfil & 1) > 0);
	

	int IDOffset = Vertices.Num();

	FVector Loc = GetActorLocation();

	if (NumX >= 2 && NumY >= 2)
	{
		FVector2D Extent = FVector2D(Offset.X,Offset.Y);

		for (int i = 0; i < NumY; i++)
		{
			for (int j = 0; j < NumX; j++)
			{
				FVector PosVertex = FVector((float)j * GridSpacing_ + Extent.X, (float)i * GridSpacing_ + Extent.Y, 0);

				Vertices.Add(PosVertex + FVector(0.f,0.f,1.f)*VerticalRangeMeters*100.f * ((i+j)%2==0?1.f:-1.f));

				UVs.Add(FVector2D(FMath::Frac(PosVertex.X/400000.f), FMath::Frac(PosVertex.Y/400000.f)));

				UV1s.Add(FVector2D(PosVertex.X/GridSpacing_, PosVertex.Y/GridSpacing_));

				UV2s.Add(FVector2D((i>0 && i<NumY-1)&&(j>0 && j<NumX-1)?1.f:0.f,0.f));
			
				
			}
		}

		for (int i = 0; i < NumY - 1; i++)
		{
			for (int j = 0; j < NumX - 1; j++)
			{
				int idx = j + (i * NumX) + IDOffset;

				if(i>0 && i<NumY - 2 && j>0 && j<NumX - 2 || NumX==2 || NumY==2|| !StitchX0 && !StitchXN && !StitchY0 && !StitchYN)
				{
					Triangles.Add(idx);
					Triangles.Add(idx + NumX);
					Triangles.Add(idx + 1);

					Triangles.Add(idx + 1);
					Triangles.Add(idx + NumX);
					Triangles.Add(idx + NumX + 1);
				}
				else
				{
					if(i==0)
					{
						if (StitchY0)
						{
							if (j % 2 == 0 && j < NumX - 2)
							{
								if (j>0)
								{
									Triangles.Add(idx);
									Triangles.Add(idx + NumX);
									Triangles.Add(idx + 1 + NumX);
								}

								Triangles.Add(idx);
								Triangles.Add(idx + 1 + NumX);
								Triangles.Add(idx + 2);

								if(j+2<NumX - 2)
								{
									Triangles.Add(idx + 2);
									Triangles.Add(idx + 1 + NumX);
									Triangles.Add(idx + 2 + NumX);
								}								

							}
						}
						else
						{
							
							if(j==0 && StitchX0)
							{
								Triangles.Add(idx);
								Triangles.Add(idx + 1 + NumX);
								Triangles.Add(idx + 1);
							}
							else if (j == NumX - 2)
							{
								Triangles.Add(idx);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1);
							}
							else
							{
								Triangles.Add(idx);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1);

								Triangles.Add(idx + 1);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + NumX + 1);
							}
							
							
						}
					}
					if (i == NumY - 2)
					{
						if (StitchYN)
						{
							if (j % 2 == 0 && j < NumX - 2)
							{
								if (j > 0)
								{
									Triangles.Add(idx);
									Triangles.Add(idx + NumX);
									Triangles.Add(idx + 1);
								}

								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 2 + NumX);
								Triangles.Add(idx + 1);

								if (j + 2 < NumX - 2)
								{
									Triangles.Add(idx + 1);
									Triangles.Add(idx + 2 + NumX);
									Triangles.Add(idx + 2);
								}

							}
						}
						else
						{
							
							if (j == 0)
							{
								Triangles.Add(idx + 1);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + NumX + 1);
							}
							else if (j == NumX - 2 && StitchXN)
							{
								Triangles.Add(idx);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1 + NumX);
							}
							else
							{
								Triangles.Add(idx);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1);

								Triangles.Add(idx + 1);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + NumX + 1);
							}
							
						}
					}
					if (j == 0)
					{
						if(StitchX0)
						{
							if (i % 2 == 0 && i < NumY - 2)
							{
								
								Triangles.Add(idx);
								Triangles.Add(idx + 2 * NumX);
								Triangles.Add(idx + NumX + 1);

								if (i > 0)
								{
									Triangles.Add(idx + 1);
									Triangles.Add(idx);
									Triangles.Add(idx + NumX + 1);
								}

								if (i + 2 < NumY - 2)
								{
									Triangles.Add(idx + 1 + NumX);
									Triangles.Add(idx + 2*NumX);
									Triangles.Add(idx + 2*NumX + 1);
								}
								

							}

						}
						else
						{
							
							if (i > 0 && i<NumY - 2)
							{
								Triangles.Add(idx);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1);

								Triangles.Add(idx + 1);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + NumX + 1);
							}
							else
							{
								if(i==0 && StitchY0)
								{
									Triangles.Add(idx);
									Triangles.Add(idx + NumX);
									Triangles.Add(idx + NumX + 1);
								}
								else
								{
									Triangles.Add(idx);
									Triangles.Add(idx + NumX);
									Triangles.Add(idx + 1);

									Triangles.Add(idx + 1);
									Triangles.Add(idx + NumX);
									Triangles.Add(idx + NumX + 1);
								}

								
							}
							
						}
						
					}
					if (j == NumX - 2)
					{

						if (StitchXN)
						{
							if (i % 2 == 0 && i < NumY - 2)
							{

								Triangles.Add(idx+1);								
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1 + 2 * NumX);

								if (i > 0)
								{
									Triangles.Add(idx + 1);
									Triangles.Add(idx);
									Triangles.Add(idx + NumX);
								}

								if (i + 2 < NumY - 2)
								{
									Triangles.Add(idx + NumX);
									Triangles.Add(idx + 2 * NumX);
									Triangles.Add(idx + 2 * NumX + 1);
								}


							}

						}
						else
						{
							
							if(i>0 && i<NumY - 2)
							{
								Triangles.Add(idx);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + 1);

								Triangles.Add(idx + 1);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + NumX + 1);
							}
							else if(i<NumY - 2)
							{
								Triangles.Add(idx + 1);
								Triangles.Add(idx + NumX);
								Triangles.Add(idx + NumX + 1);
							}
							else
							{

							
							}
							

							
						}
						
						
						
						
					}


				}


			}
		}
	}
}

void AGeometryClipMapWorld::UpdateViewFrustum()
{
	if(Meshes.Num()>0 && Meshes[0].Mesh)
	{
		TArray<FConvexVolume> Frustums = Meshes[0].Mesh->GetViewsFrustums();		

		for(int i=0; i<Frustums.Num();i++)
		{
			if(Frustums[i].Planes.Num()>0)
			{
				ViewFrustum = Frustums[i];
			}			
		}
	}
}

void AGeometryClipMapWorld::UpdateCameraLocation()
{

	UWorld* World = GetWorld();

	if (World && World->ViewLocationsRenderedLastFrame.Num())
	{
		CamLocation = World->ViewLocationsRenderedLastFrame[0];		
	}
	
}

float AGeometryClipMapWorld::HeightToClosestCollisionMesh()
{
	UProceduralMeshComponent* ClosestMesh = nullptr;
	float ClosestDistance = -1.f;

	for(int& i : UsedCollisionMesh)
	{
		FVector CompToCam = CollisionMesh[i].Mesh->GetComponentLocation()-CamLocation;
		CompToCam.Z=0.f;

		if(ClosestDistance<0.f || CompToCam.Size()<ClosestDistance)
		{
			ClosestDistance=CompToCam.Size();
			ClosestMesh = CollisionMesh[i].Mesh;
		}

	}

	if(ClosestMesh)
	{
		FProcMeshSection* Section = ClosestMesh->GetProcMeshSection(0);

		return (Section->SectionLocalBox.GetCenter()+GetActorLocation().Z*FVector(0.f,0.f,1.f) -CamLocation).Z;
	}

	return -1.f;
}

void AGeometryClipMapWorld::UpdateClipMap()
{

	


	float Height = FMath::Abs((CamLocation - GetActorLocation()).Z);
	float RelevantHeight = 0.f;

	if (GenerateCollision )
	{
		RelevantHeight = HeightToClosestCollisionMesh();


		Height=FMath::Abs(RelevantHeight);
	}

	for(int i=0; i< Meshes.Num(); i++)
	{
		FClipMapMeshElement& Elem = Meshes[i];
		
		if(!Elem.Mesh && !Elem.I_Mesh)
			continue;

		if(Height>Elem.GridSpacing*0.4f*N && Elem.Level>0)
		{
			if(Elem.IsSectionVisible(0))
				Elem.SetSectionVisible(0,false);
			if(Elem.IsSectionVisible(1))
				Elem.SetSectionVisible(1,false);
			if(Elem.IsSectionVisible(2))
				Elem.SetSectionVisible(2,false);
			if(Elem.IsSectionVisible(3))
				Elem.SetSectionVisible(3,false);
			if(Elem.IsSectionVisible(4))
				Elem.SetSectionVisible(4,false);
			if(Elem.IsSectionVisible(5))
				Elem.SetSectionVisible(5,false);

			Elem.Config = EClipMapInteriorConfig::NotVisible;
		}
		else
		{
			if(Height>Elem.GridSpacing*0.4f*N/2.f || Elem.Level==LOD_Num-1)
			{
				
				if(!Elem.IsSectionVisible(0))
					Elem.SetSectionVisible(0,true);
				if(Elem.IsSectionVisible(1))
					Elem.SetSectionVisible(1, false);
				if(Elem.IsSectionVisible(2))
					Elem.SetSectionVisible(2, false);
				if(Elem.IsSectionVisible(3))
					Elem.SetSectionVisible(3, false);
				if(Elem.IsSectionVisible(4))
					Elem.SetSectionVisible(4, false);
				if(Elem.IsSectionVisible(5))
					Elem.SetSectionVisible(5, false);

				Elem.Config = EClipMapInteriorConfig::NotVisible;
				
			}
			else
			{
				if(Elem.IsSectionVisible(0))
					Elem.SetSectionVisible(0, false);
				if(!Elem.IsSectionVisible(1))
					Elem.SetSectionVisible(1, true);

			}

				FVector CompToCam = CamLocation - Elem.Location;
				float MaxPlanarOffset = FMath::Max(FMath::Abs(CompToCam.X),FMath::Abs(CompToCam.Y));
			

				if(MaxPlanarOffset>Elem.GridSpacing)
				{

					FVector CompLoc = CamLocation/(2.0*Elem.GridSpacing);

					FVector LocRef = 2.0*Elem.GridSpacing*FVector(FMath::Floor(CompLoc.X),FMath::Floor(CompLoc.Y),0.f)+GetActorLocation().Z *FVector(0.f,0.f,1);

					FVector Diff = (CamLocation-LocRef);//Elem.GridSpacing;

					FVector ToLoc = FVector(0.f,0.f,0.f);

					
					{
						ToLoc = -Elem.GridSpacing*FVector(1.f,1.f,0.f);

						if (Diff.X > 0.f && Diff.Y > 0.f)
						{
							ToLoc += 2.0 * Elem.GridSpacing * FVector(1.f, 1.f, 0.f);
						}
						else if (Diff.X > 0.f && Diff.Y <= 0.f)
						{
							ToLoc += 2.0 * Elem.GridSpacing * FVector(1.f, 0.f, 0.f);
						}
						else if (Diff.X <= 0.f && Diff.Y > 0.f)
						{
							ToLoc = 2.0 * Elem.GridSpacing * FVector(0.f, 1.f, 0.f);
						}
					}
					
					
					Elem.Location=LocRef + ToLoc;

					if(Elem.Mesh)
					{
						Elem.Mesh->SetWorldLocation(LocRef + ToLoc, false, nullptr, ETeleportType::TeleportPhysics);
						Elem.Mesh->MarkRenderTransformDirty();
					}
					if(Elem.I_Mesh)
					{
						Elem.I_Mesh->SetWorldLocation(LocRef + ToLoc, false, nullptr, ETeleportType::TeleportPhysics);
						Elem.I_Mesh->MarkRenderTransformDirty();
					}
					

					if(EnableCaching && ComputeCacheMat)
					{
						if (Elem.CacheMatDyn)
						{
							Elem.CacheMatDyn->SetVectorParameterValue("RingLocation", Elem.Location);
							Elem.CacheMatDyn->SetScalarParameterValue("NormalMapSelect", 0.f);
							UKismetRenderingLibrary::ClearRenderTarget2D(this, Elem.HeightMap, FLinearColor::Black);
							UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.HeightMap, Elem.CacheMatDyn);
							Elem.CacheMatDyn->SetScalarParameterValue("NormalMapSelect", 1.f);
							UKismetRenderingLibrary::ClearRenderTarget2D(this, Elem.NormalMap, FLinearColor::Black);
							UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.NormalMap, Elem.CacheMatDyn);

							
							for(int k=0; k<Elem.LandLayers.Num();k++)
							{
								if(!Elem.LayerMatDyn[k] || !Elem.LandLayers[k])
								{
									UE_LOG(LogTemp,Warning,TEXT("ERROR drawing layers: !Elem.LayerMatDyn[%d] || !Elem.LandLayers[%d]"),k,k);
									continue;
								}

								
								Elem.LayerMatDyn[k]->SetVectorParameterValue("RingLocation", Elem.Location);
								

								UKismetRenderingLibrary::ClearRenderTarget2D(this, Elem.LandLayers[k], FLinearColor::Black);
								UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Elem.LandLayers[k], Elem.LayerMatDyn[k]);
							}
							
						}
						else
						{
							UE_LOG(LogTemp,Warning,TEXT("ERROR Recreating the clipmap cache computation materials - should not be happening"));
						}
					}
					

					
					if(Elem.MatDyn)
						Elem.MatDyn->SetVectorParameterValue("RingLocation",Elem.Location);
					else
					{
						UMaterialInstanceDynamic* MatDyn_ = nullptr;
						if(Elem.Mesh)
							MatDyn_ = Cast<UMaterialInstanceDynamic>(Elem.Mesh->GetMaterial(0));
						if (Elem.I_Mesh)
							MatDyn_ = Cast<UMaterialInstanceDynamic>(Elem.I_Mesh->GetMaterial(0));

						if (MatDyn_)
							MatDyn_->SetVectorParameterValue("RingLocation", Elem.Location);
					
					}


					

					if (Elem.Level > 0)
						UpdateParentInnerMesh(Elem.Level, RelativeLocationToParentInnerMeshConfig(Elem.Location - Meshes[Elem.Level - 1].Location));

					

					
				}

				//TODO Remove ?
				if (Elem.Level > 0 && Meshes[Elem.Level - 1].Config!=RelativeLocationToParentInnerMeshConfig(Elem.Location - Meshes[Elem.Level - 1].Location))
					UpdateParentInnerMesh(Elem.Level, RelativeLocationToParentInnerMeshConfig(Elem.Location - Meshes[Elem.Level - 1].Location));

			

			
		}
	}

	
	if (DataReceiver)
		DataReceiver->UpdateStaticDataFor(this);
}

void AGeometryClipMapWorld::UpdateCollisionMesh()
{



		FVector CompLoc = CamLocation / (CollisionMeshWorldDimension);
		int CamX = FMath::RoundToInt(CompLoc.X);
		int CamY = FMath::RoundToInt(CompLoc.Y);


		FVector LocRef = CollisionMeshWorldDimension * FVector(CamX, CamY, 0.f) + GetActorLocation().Z * FVector(0.f, 0.f, 1);

		for(int i = UsedCollisionMesh.Num()-1 ;i>=0 ; i--)
		{
			FCollisionMeshElement& El = CollisionMesh[UsedCollisionMesh[i]];
			FVector ToComp = (El.Mesh->GetComponentLocation()-LocRef)/CollisionMeshWorldDimension;
			if(FMath::Abs(ToComp.X)>CollisionMeshPerQuadrantAroundPlayer+.1f || FMath::Abs(ToComp.Y)>CollisionMeshPerQuadrantAroundPlayer+.1f)
			{
				
				
				AvailableCollisionMesh.Add(El.ID);
				UsedCollisionMesh.RemoveAt(i);
				
				
				for (auto It = GroundCollisionLayout.CreateConstIterator(); It; ++It)
				{
					if (It->Value.Mesh == El.Mesh)
					{
						GroundCollisionLayout.Remove(It->Key);
						break;
					}
				}
				
			}
				
		}

		

		for(int i =-CollisionMeshPerQuadrantAroundPlayer; i<=CollisionMeshPerQuadrantAroundPlayer; i++)
		{
			for (int j = -CollisionMeshPerQuadrantAroundPlayer; j <= CollisionMeshPerQuadrantAroundPlayer; j++)
			{
				FIntVector LocMeshInt = FIntVector(CamX+i,CamY+j,0);

				FVector MeshLoc = CollisionMeshWorldDimension*FVector(LocMeshInt) + GetActorLocation().Z * FVector(0.f, 0.f, 1);
				

				if(!GroundCollisionLayout.Contains(LocMeshInt))
				{
					FCollisionMeshElement& Mesh = GetACollisionMesh();

					Mesh.Location=MeshLoc;
					Mesh.Mesh->SetWorldLocation(MeshLoc, false, nullptr, ETeleportType::TeleportPhysics);

					UpdateCollisionMeshData(Mesh);					
				
					GroundCollisionLayout.Add(LocMeshInt,Mesh);

				}

			}
		
		}

		

}


double AGeometryClipMapWorld::GetHeightFromGPURead(FColor& ReadLoc)
{

	int Height = 0;
	//int* HeightPtr = &Height;
	uint8* HeightAs8 = reinterpret_cast<uint8*>(&Height);

	HeightAs8[0] = ReadLoc.A;
	HeightAs8[1] = ReadLoc.B;
	HeightAs8[2] = ReadLoc.G;
	HeightAs8[3] = ReadLoc.R;



	return Height;



}
void AGeometryClipMapWorld::ProcessCollisionsPending()
{
	//TODO add physic material support ?
	if (rebuild)
		return;

	for(int& ElID : CollisionReadToProcess)
	{
	
		FCollisionMeshElement& Mesh = CollisionMesh[ElID];
		if (!Mesh.Mesh)
		{
			rebuild = true;
			return;
		}

		FVector MesgLoc = Mesh.Mesh->GetComponentLocation();
		//SetProcMeshSection
		FProcMeshSection* Section = Mesh.Mesh->GetProcMeshSection(0);

		const int NumOfVertex = Section->ProcVertexBuffer.Num();

		TArray<FVector> Vertices;
		Vertices.SetNum(NumOfVertex);
		TArray<FVector> Normals;
		Normals.SetNum(NumOfVertex);
		TArray<FVector2D> UV;
		UV.SetNum(NumOfVertex);
		TArray<FColor> Colors;
		Colors.SetNum(NumOfVertex);

	
		

		ParallelFor(NumOfVertex, [&](int32 k)
		{
			if(k<NumOfVertex)
			{

				float height = GetHeightFromGPURead(Mesh.HeightData[k]);

				FVector LocationfVertice_WS = Section->ProcVertexBuffer[k].Position * FVector(1.f, 1.f, 0.f) + height * FVector(0.f, 0.f, 1.f);
				

				//Section->ProcVertexBuffer[k].Position = LocationfVertice_WS;

				Vertices[k] = LocationfVertice_WS;
				Normals[k] = FVector(0.f, 0.f, 1.f);
				UV[k] = FVector2D(0.f, 0.f);
				Colors[k] = FColor::Blue;

			}
			
		});

		TArray<FProcMeshTangent> Tangents;
		Tangents.Init(FProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());

		//continue;

		Mesh.Mesh->UpdateMeshSection(0,Vertices,Normals,UV,Colors,Tangents);
		//updatecollisionmesh

		if(HasActorBegunPlay())
			Mesh.Mesh->ClearCollisionConvexMeshes();
		// 		
	}

	CollisionReadToProcess.Empty();

}

double AGeometryClipMapWorld::ComputeWorldHeightAt(FVector WorldLocation)
{
	//Implement your noise here // same one as the one in shader



	return 0.f;


}

void ReadPixelsFromRT(UTextureRenderTarget2D* InRT, FCollisionMeshElement& Mesh)
{
	ENQUEUE_RENDER_COMMAND(ReadGeoClipMapRTCmd)(
		[InRT, &Mesh](FRHICommandListImmediate& RHICmdList)
	{		
		check(IsInRenderingThread());

			Mesh.HeightData.SetNum(InRT->GetRenderTargetResource()->GetSizeX()*InRT->GetRenderTargetResource()->GetSizeX(),false);
			RHICmdList.ReadSurfaceData(
				InRT->GetRenderTargetResource()->GetRenderTargetTexture(),
				FIntRect(0, 0, InRT->GetRenderTargetResource()->GetSizeX(), InRT->GetRenderTargetResource()->GetSizeY()),
				Mesh.HeightData,
				FReadSurfaceDataFlags());
			
		
		
	});
	FlushRenderingCommands();
}

void AGeometryClipMapWorld::UpdateCollisionMeshData(FCollisionMeshElement& Mesh)
{
	// Couple options i see here, either make a readback from a render target applying the same noise than the geoclipmap mesh
	// or implement the same noise in C++ and compute it in parallel/on another thread

	FVector MesgLoc = Mesh.Mesh->GetComponentLocation();

	
	if (CollisionMat_HeightRead)
	{
		//OPTION A : Compute collision form GPU readback

		UMaterialInstanceDynamic* DynCollisionMat = UMaterialInstanceDynamic::Create(CollisionMat_HeightRead, this);
		DynCollisionMat->SetVectorParameterValue("MeshLocation",MesgLoc);
		DynCollisionMat->SetScalarParameterValue("CollisionVerticeNum",CollisionMeshVerticeNumber);
		DynCollisionMat->SetScalarParameterValue("CollisionDimension",CollisionMeshWorldDimension);
		DynCollisionMat->SetScalarParameterValue("MeshScale",CollisionMeshWorldDimension*(CollisionMeshVerticeNumber<=1? 1 :CollisionMeshVerticeNumber/(CollisionMeshVerticeNumber-1)));
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Mesh.CollisionRT, FLinearColor::Black);
		//Mesh.CollisionRT->UpdateResourceImmediate();

		UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, Mesh.CollisionRT, DynCollisionMat);

		Mesh.HeightData.Empty();
		Mesh.HeightData.SetNum(CollisionMeshVerticeNumber*CollisionMeshVerticeNumber,false);


		ReadPixelsFromRT(Mesh.CollisionRT,Mesh);

		CollisionReadToProcess.Add(Mesh.ID);
		
		return;
	}
	
	return;

	//OPTION B : Implement in c++ the same noise as the one in Shader graph and evaluate the noise here to generate the collision mesh
	
	

	FProcMeshSection* Section = Mesh.Mesh->GetProcMeshSection(0);

	int NumOfVertex = Section->ProcVertexBuffer.Num();

	TArray<FVector> Vertices;
	Vertices.SetNum(NumOfVertex);
	TArray<FVector> Normals;
	Normals.SetNum(NumOfVertex);
	TArray<FVector2D> UV;
	UV.SetNum(NumOfVertex);
	TArray<FColor> Colors;
	Colors.SetNum(NumOfVertex);

	
	ParallelFor(NumOfVertex, [&](int32 k)
	{
		FVector LocationfVertice_WS = Section->ProcVertexBuffer[k].Position*FVector(1.f,1.f,0.f) + MesgLoc;

		// ComputeWorldHeightAt is empty / not Implemented

		LocationfVertice_WS.Z = ComputeWorldHeightAt(LocationfVertice_WS);

		Vertices[k] = LocationfVertice_WS - Mesh.Mesh->GetComponentLocation();
		Normals[k] = FVector(0.f,0.f,1.f);
		UV[k] = FVector2D(0.f,0.f);
		Colors[k] = FColor::Blue;
	});

	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());


	Mesh.Mesh->UpdateMeshSection(0,Vertices,Normals,UV,Colors,Tangents);

}

FTransform AGeometryClipMapWorld::GetLocalTransformOfSpawnable(const FVector& CompLoc, FColor& LocX,FColor& LocY,FColor& LocZ,FColor& Rot)
{
	int X = 0;
	uint8* XAs8 = reinterpret_cast<uint8*>(&X);

	XAs8[0] = LocX.A;
	XAs8[1] = LocX.B;
	XAs8[2] = LocX.G;
	XAs8[3] = LocX.R;

	int Y = 0;
	uint8* YAs8 = reinterpret_cast<uint8*>(&Y);

	YAs8[0] = LocY.A;
	YAs8[1] = LocY.B;
	YAs8[2] = LocY.G;
	YAs8[3] = LocY.R;

	int Z = 0;
	uint8* ZAs8 = reinterpret_cast<uint8*>(&Z);

	ZAs8[0] = LocZ.A;
	ZAs8[1] = LocZ.B;
	ZAs8[2] = LocZ.G;
	ZAs8[3] = LocZ.R;


	// Rot.R : Yaw
	// Rot.G : Pitch
	// Rot.B : Roll
	// Rot.A : Scale

	float Yaw = ((float)Rot.R)/255.f*360.f;
	float Pitch = ((float)Rot.G)/255.f*360.f;
	float Roll = ((float)Rot.B)/255.f*360.f;

	float Scale = ((float)Rot.A)/255.f*3.f;


	return FTransform(FRotator(Pitch,Yaw,Roll).Quaternion(),FVector(X,Y,Z)-CompLoc,FVector(Scale,Scale,Scale));

}

void AGeometryClipMapWorld::ProcessSpawnablePending()
{
	if (rebuild)
		return;

	for (FSpawnableMesh& Spawn : Spawnables)
	{
		
		for (int& ElID : Spawn.SpawnablesElemReadToProcess)
		{
			//Spawn.SpawnablesElem
			FSpawnableMeshElement& Mesh = Spawn.SpawnablesElem[ElID];

			FVector MesgLoc = Mesh.Location;
			
			const int NumOfVertex = Spawn.RT_Dim*Spawn.RT_Dim;
			

			// The idea is that if we use instanced meshes, their transform are computed in local space, while actors require world space			
			const FVector CompLocation = Spawn.SpawnType == ESpawnableType::Mesh ? GetActorLocation() : FVector(0.f,0.f,0.f);

			TArray<TArray<FTransform>> InstancesT;

			if (Spawn.SpawnType == ESpawnableType::Mesh)
				InstancesT.SetNum(Spawn.HIM_Mesh.Num(), false);
			else
				InstancesT.SetNum(Spawn.Spawned_Actors.Num(), false);


			for(int i = 0; i< InstancesT.Num();i++)
			{
				TArray<FTransform>& T = InstancesT[i];
				T.SetNum(Spawn.NumInstancePerHIM[i], false);
			}

			if(NumOfVertex<5)
			{
				for(int k=0;k<NumOfVertex;k++)
				{
					(InstancesT[Spawn.InstanceIndexToHIMIndex[k]])[Spawn.InstanceIndexToIndexForHIM[k]] = GetLocalTransformOfSpawnable(CompLocation, Mesh.LocationXData[k], Mesh.LocationYData[k], Mesh.LocationZData[k], Mesh.RotationData[k]);
				}
				
			}
			else
			{
				ParallelFor(NumOfVertex, [&](int32 k)
				{
					if (k < NumOfVertex)
					{
						(InstancesT[Spawn.InstanceIndexToHIMIndex[k]])[Spawn.InstanceIndexToIndexForHIM[k]] = GetLocalTransformOfSpawnable(CompLocation, Mesh.LocationXData[k], Mesh.LocationYData[k], Mesh.LocationZData[k], Mesh.RotationData[k]);

					}

				});
			}

		/**
		* We allow actors to be destroyed. If the actor was destroyed, spawn a new one, otherwise move a preexisting one.
		*/

			if(Mesh.InstancesIndexes.Num()>0)
			{
				if (Spawn.SpawnType == ESpawnableType::Mesh)
				for (int i = 0; i < Spawn.HIM_Mesh.Num(); i++)
				{									
					Spawn.HIM_Mesh[i]->BatchUpdateInstancesTransforms(Mesh.InstanceOffset[i],InstancesT[i],false,true);
				}
				else
				{
					for (int i = 0; i < Spawn.Spawned_Actors.Num(); i++)
					{
						FSpawnedActorList& SAL = Spawn.Spawned_Actors[i];
						int FirstIndice = Mesh.InstanceOffset[i];

						for (int j = 0; j < InstancesT[i].Num(); j++)
						{				
							FTransform& T = InstancesT[i][j];
							if(T.GetScale3D().X>0.0001f)
							{
								

								if (AActor* Local_Actor = SAL.SpawnedActors[FirstIndice + j])
								{									
									Local_Actor->SetActorTransform(T, false, nullptr, ETeleportType::TeleportPhysics);
								}
								else
								{
									FActorSpawnParameters ActorSpawnParameters;
									ActorSpawnParameters.ObjectFlags=RF_Transient;									
									
									AActor* SpawnedActor = GetWorld()->SpawnActor(Spawn.Actors_Validated[i].Get(), &T, ActorSpawnParameters);
									SAL.SpawnedActors[FirstIndice + j] = SpawnedActor;
								}
							}
							
						}						
					}
				}
			}
			else
			{

				Mesh.InstancesIndexes.SetNum(Spawn.NumInstancePerHIM.Num());
				Mesh.InstanceOffset.SetNum(Spawn.NumInstancePerHIM.Num());

				if (Spawn.SpawnType == ESpawnableType::Mesh)
				for(int i=0; i<Spawn.HIM_Mesh.Num();i++)
				{
					Mesh.InstanceOffset[i] = Spawn.HIM_Mesh[i]->GetInstanceCount();
					Mesh.InstancesIndexes[i].InstancesIndexes = Spawn.HIM_Mesh[i]->AddInstances(InstancesT[i],true);	
				}
				else
				{
					for (int i = 0; i < Spawn.Spawned_Actors.Num(); i++)
					{
						FSpawnedActorList& SAL = Spawn.Spawned_Actors[i];
						Mesh.InstanceOffset[i] = SAL.SpawnedActors.Num();

						for (int j = 0; j < InstancesT[i].Num(); j++)
						{	
							Mesh.InstancesIndexes[i].InstancesIndexes.Add(SAL.SpawnedActors.Num());
							
							FTransform& T = InstancesT[i][j];
							if (T.GetScale3D().X > 0.0001f)
							{
								FActorSpawnParameters ActorSpawnParameters;
								ActorSpawnParameters.ObjectFlags = RF_Transient;				

								AActor* SpawnedActor = GetWorld()->SpawnActor(Spawn.Actors_Validated[i].Get(), &T,ActorSpawnParameters);
								SAL.SpawnedActors.Add(SpawnedActor);
								
							}
							else 
								SAL.SpawnedActors.Add(nullptr);

						}

						
						
					}
				}
	
			}
			
		}

		Spawn.SpawnablesElemReadToProcess.Empty();
	}
}

EClipMapInteriorConfig AGeometryClipMapWorld::RelativeLocationToParentInnerMeshConfig(FVector RelativeLocation)
{
	if(RelativeLocation.X>0 && RelativeLocation.Y>0)
		return EClipMapInteriorConfig::BotLeft;
	else if (RelativeLocation.X > 0 && RelativeLocation.Y <= 0)
		return EClipMapInteriorConfig::BotRight ;
	else if (RelativeLocation.X <= 0 && RelativeLocation.Y > 0)
		return EClipMapInteriorConfig::TopLeft;
	else
		return EClipMapInteriorConfig::TopRight;

}

void AGeometryClipMapWorld::UpdateParentInnerMesh(int ChildLevel,EClipMapInteriorConfig NewConfig)
{
	
	int ParentIndice = ChildLevel-1;
	if(ParentIndice>=0)
	{
		//if(Meshes[ParentIndice].Config!=NewConfig)
		{
			bool VisibilityFlag[4]={NewConfig==EClipMapInteriorConfig::BotLeft,NewConfig==EClipMapInteriorConfig::TopLeft,NewConfig==EClipMapInteriorConfig::BotRight,NewConfig==EClipMapInteriorConfig::TopRight};
			for(int i=0;i<4;i++)
			{
				Meshes[ParentIndice].SetSectionVisible(2+i,VisibilityFlag[i]);
			}

			Meshes[ParentIndice].Config = NewConfig;
		}
	}
}

FCollisionMeshElement& AGeometryClipMapWorld::GetACollisionMesh()
{
	if(AvailableCollisionMesh.Num()>0)
	{
		FCollisionMeshElement& Elem = CollisionMesh[AvailableCollisionMesh[AvailableCollisionMesh.Num()-1]];
		UsedCollisionMesh.Add(Elem.ID);
		AvailableCollisionMesh.RemoveAt(AvailableCollisionMesh.Num()-1);
		return Elem;
	}

	FCollisionMeshElement NewElem;
	NewElem.ID=CollisionMesh.Num();

	UWorld* World = GetWorld();

	uint32 SizeT = (uint32)CollisionMeshVerticeNumber;

	RendertargetMemoryBudgetMB+=(SizeT*SizeT*4)/1000000.0f;

	NewElem.CollisionRT = UKismetRenderingLibrary::CreateRenderTarget2D(World, SizeT, SizeT, RTF_RGBA8,
		FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
	NewElem.CollisionRT->AddToRoot();
#endif

	NewElem.CollisionRT->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
	
	NewElem.CollisionRT->UpdateResourceImmediate();



	NewElem.Mesh = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);

#if !WITH_EDITOR
	NewElem.Mesh->AddToRoot();
#endif
	NewElem.Mesh->bUseAsyncCooking=true;

	NewElem.Mesh->SetupAttachment(RootComponent);
	NewElem.Mesh->RegisterComponent();

	NewElem.Mesh->bUseComplexAsSimpleCollision = true;

	NewElem.Mesh->SetRelativeLocation(FVector(0.f,0.f, 0.f));

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector2D> UV;

	float Spacing = CollisionMeshWorldDimension/(CollisionMeshVerticeNumber<=1? 1:CollisionMeshVerticeNumber-1);

	UKismetProceduralMeshLibrary::CreateGridMeshWelded(CollisionMeshVerticeNumber,CollisionMeshVerticeNumber,Triangles,Vertices,UV,Spacing);

	TArray<FVector> Normals;
	Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
	TArray<FColor> Colors;
	Colors.Init(FColor::Blue, Vertices.Num());
	TArray<FProcMeshTangent> Tangents;
	Tangents.Init(FProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());

	NewElem.Mesh->CreateMeshSection(0,Vertices,Triangles,Normals,UV,Colors,Tangents,true);
	
	if(CollisionMat)
		NewElem.Mesh->SetMaterial(0,CollisionMat);

	UsedCollisionMesh.Add(NewElem.ID);
	CollisionMesh.Add(NewElem);

	return CollisionMesh[CollisionMesh.Num()-1];
}

void AGeometryClipMapWorld::ReleaseCollisionMesh(int ID)
{
	if(ID>=0 && ID< CollisionMesh.Num())
	{		
		AvailableCollisionMesh.Add(ID);
		
	}
	else
	{
		UE_LOG(LogTemp,Warning,TEXT("cant release mesh"));
	}
}

void AGeometryClipMapWorld::InitiateWorld()
{
	if(Meshes.Num()>0)
		return;

	RendertargetMemoryBudgetMB=0;

	GridSpacing=WorldDimensionMeters*100/(N-1);

	for(int i=0; i<LOD_Num;i++)
	{
		int LOD = LOD_Num-1-i;
		FClipMapMeshElement NewElem;	

		NewElem.Level=i;
		NewElem.GridSpacing=pow(2.0,-i)*GridSpacing;

		int LocalM = (N+1)/4;

		int CacheRes = (i< LOD_Num-LOD_above_doubleCacheResolution  /*Level/2*/?2.0f:1.0f)*ClipMapCacheIntraVerticesTexel*(N-1) +1;
		//int CacheRes = ClipMapCacheIntraVerticesTexel*N;



		if(EnableCaching)
		{
			RendertargetMemoryBudgetMB+=2*(CacheRes*CacheRes*4)/1000000.0f;

			NewElem.HeightMap = UKismetRenderingLibrary::CreateRenderTarget2D(GetWorld(), CacheRes, CacheRes, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
			NewElem.HeightMap->AddToRoot();
#endif
			NewElem.HeightMap->Filter = TF_Nearest;
			NewElem.HeightMap->AddressX = TA_Clamp;
			NewElem.HeightMap->AddressY = TA_Clamp;
			NewElem.HeightMap->UpdateResourceImmediate();

			NewElem.NormalMap = UKismetRenderingLibrary::CreateRenderTarget2D(GetWorld(), CacheRes, CacheRes, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
			NewElem.NormalMap->AddToRoot();
#endif
			NewElem.NormalMap->AddressX = TA_Clamp;
			NewElem.NormalMap->AddressY = TA_Clamp;
			NewElem.NormalMap->UpdateResourceImmediate();

			for(FClipMapLayer& layer : LandDataLayers)
			{
				if(layer.LayerName!="" && layer.MaterialToGenerateLayer)
				{
					RendertargetMemoryBudgetMB+=(CacheRes*CacheRes*4)/1000000.0f;

					UTextureRenderTarget2D* Layer = UKismetRenderingLibrary::CreateRenderTarget2D(GetWorld(), CacheRes, CacheRes, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
					Layer->AddToRoot();
#endif
					Layer->AddressX = TA_Clamp;
					Layer->AddressY = TA_Clamp;
					Layer->UpdateResourceImmediate();
					NewElem.LandLayers.Add(Layer);
					NewElem.LandLayers_names.Add(FName(*layer.LayerName));

				}
			}
			
		}

		NewElem.Location = RootComponent->GetComponentLocation() + FVector(-NewElem.GridSpacing, -NewElem.GridSpacing, 0.f);
		
		if(WorldPresentation==EWorldPresentation::InstancedMesh)
		{
			NewElem.I_Mesh = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
#if !WITH_EDITOR
			NewElem.I_Mesh->AddToRoot();
#endif
			NewElem.I_Mesh->SetupAttachment(RootComponent);
			NewElem.I_Mesh->RegisterComponent();

			
			if(VisualRepresentation)
				NewElem.I_Mesh->SetStaticMesh(VisualRepresentation);

			NewElem.I_Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			NewElem.I_Mesh->NumCustomDataFloats=6;			
			NewElem.I_Mesh->bNeverDistanceCull = true;
			NewElem.I_Mesh->SetRelativeLocation(FVector(-NewElem.GridSpacing, -NewElem.GridSpacing, 0.f));
			NewElem.I_Mesh->CastShadow=true;
			NewElem.I_Mesh->bCastFarShadow=true;
			
		}
		else
		{
			NewElem.Mesh = NewObject<UGeoClipmapMeshComponent>(this, NAME_None, RF_Transient);
#if !WITH_EDITOR
			NewElem.Mesh->AddToRoot();
#endif
			NewElem.Mesh->SetupAttachment(RootComponent);
			NewElem.Mesh->RegisterComponent();

			NewElem.Mesh->bUseComplexAsSimpleCollision = true;
			NewElem.Mesh->bNeverDistanceCull = true;

			NewElem.Mesh->CastShadow = true;
			NewElem.Mesh->bCastFarShadow = true;

			NewElem.Mesh->SetRelativeLocation(FVector(-NewElem.GridSpacing, -NewElem.GridSpacing, 0.f));
		}

		


		
		

		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector2D> UV;
		TArray<FVector2D> UV1;
		TArray<FVector2D> UV2;
		TArray<FVector2D> UV_dummy;
		TArray<FColor> Color_dummy;


		float LocalExtent = ((N - 1) * NewElem.GridSpacing) / 2.f;
		FVector LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f);	

		// StichingProfile
		// 1<<3 X=0
		// 1<<2 X=N-1
		// 1<<1 Y=0
		// 1 Y=N-1
		uint8 StichingProfile = 1<<3|1<<2|1<<1|1;

		if(NewElem.I_Mesh)
			StichingProfile = 0;

		
		CreateGridMeshWelded(LOD,N,N,Triangles,Vertices,UV,UV1,UV2,NewElem.GridSpacing,LocalOffset,StichingProfile);

		if(NewElem.I_Mesh)
		{
			int NumI = Vertices.Num();
			TArray<FTransform> InstanceT;
			InstanceT.SetNum(NumI);
			float ScaleLocal = pow(2.0,-i+LOD_Num-1);

			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					FTransform& T = InstanceT[k];
					T.SetLocation(Vertices[k]);		
					T.SetRotation(FQuat::Identity);
					T.SetScale3D(NewElem.GridSpacing/100.f* FVector(1.f,1.f,1.f));
				}

			});

			const TArray<int32> Indexes = NewElem.I_Mesh->AddInstances(InstanceT,true);

			if(!(Indexes.Num()!=NumI || UV.Num()!=NumI|| UV1.Num()!=NumI))
			
				ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					TArray<float> CustomData;
					//Section Indentifier
					CustomData.Add(0.f);
					CustomData.Add(UV[k].X);
					CustomData.Add(UV[k].Y);
					CustomData.Add(UV1[k].X);
					CustomData.Add(UV1[k].Y);
					CustomData.Add(Vertices[k].Z);

					NewElem.I_Mesh->SetCustomData(Indexes[k], CustomData);
				}

			});
			else
			{
				UE_LOG(LogTemp,Warning,TEXT("Indexes.Num %d UV num %d UV1 num %d"),Indexes.Num(),UV.Num(),UV1.Num());
			}


						
		}

		//stiching



		
		TArray<FVector> Normals;
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		TArray<FGeoCProcMeshTangent> Tangents;
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());

		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(0,Vertices,Triangles,Normals,UV,UV1,UV2,UV_dummy,Color_dummy,Tangents,false);
		


		Vertices.Empty();
		Triangles.Empty();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();
		
		StichingProfile = 1<<3|1<<2|1<<1;
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,N,3,Triangles,Vertices,UV,UV1,UV2,NewElem.GridSpacing,LocalOffset,StichingProfile);		

		
		StichingProfile = 0;
		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f) + 2.f*NewElem.GridSpacing*FVector(0.f,1.f,0.f)  + (LocalM-1)*NewElem.GridSpacing*FVector(1.f,0.f,0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,(LocalM-1)*2+3,(LocalM-1)-2+1, Triangles, Vertices, UV, UV1,UV2, NewElem.GridSpacing, LocalOffset, StichingProfile);
		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f) + ((LocalM-1)*3 + 2)*NewElem.GridSpacing*FVector(0.f,1.f,0.f)  + (LocalM-1)*NewElem.GridSpacing*FVector(1.f,0.f,0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,(LocalM-1)*2+3, (LocalM-1)-2+1, Triangles, Vertices, UV, UV1,UV2, NewElem.GridSpacing, LocalOffset, StichingProfile);
		
		
		StichingProfile = 1<<3|1<<2|1;
		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f)+ ((N-1) - 2) *NewElem.GridSpacing*FVector(0.f,1.f,0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,N,3,Triangles,Vertices,UV,UV1,UV2,NewElem.GridSpacing,LocalOffset,StichingProfile);
		
		StichingProfile = 1<<3;
		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f) + (2) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,LocalM, N-4 /*LocalM*2+1+2*/, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		StichingProfile = 1<<2;
		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + (2) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f) + ((LocalM - 1)*3+2) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f) ;
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,LocalM, N-4/*LocalM*2+1 +2*/, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());

		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(1, Vertices, Triangles, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);

		if (NewElem.I_Mesh)
		{
			int NumI = Vertices.Num();
			TArray<FTransform> InstanceT;
			InstanceT.SetNum(NumI);
			float ScaleLocal = pow(2.0, -i + LOD_Num - 1);

			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					FTransform& T = InstanceT[k];
					T.SetLocation(Vertices[k]);
					T.SetRotation(FQuat::Identity);
					T.SetScale3D(NewElem.GridSpacing/100.f*FVector(1.f, 1.f, 1.f));
				}

			});

			TArray<int32> Indexes = NewElem.I_Mesh->AddInstances(InstanceT,true);

			if(!(Indexes.Num()!=NumI || UV.Num()!=NumI|| UV1.Num()!=NumI))
			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					TArray<float> CustomData;
					//Section Indentifier
					CustomData.Add(1.f);
					CustomData.Add(UV[k].X);
					CustomData.Add(UV[k].Y);
					CustomData.Add(UV1[k].X);
					CustomData.Add(UV1[k].Y);
					CustomData.Add(Vertices[k].Z);
					NewElem.I_Mesh->SetCustomData(Indexes[k], CustomData);
				}

			});
		}


		Vertices.Empty();
		Triangles.Empty();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();
		//inner L Shape have no stiching
		StichingProfile = 0;

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,LocalM*2+1, 2, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,2, LocalM * 2, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// botleft
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(2, Vertices, Triangles, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false); 

		if (NewElem.I_Mesh)
		{
			int NumI = Vertices.Num();
			TArray<FTransform> InstanceT;
			InstanceT.SetNum(NumI);
			float ScaleLocal = pow(2.0, -i + LOD_Num - 1);

			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					FTransform& T = InstanceT[k];
					T.SetLocation(Vertices[k]);
					T.SetRotation(FQuat::Identity);
					T.SetScale3D(NewElem.GridSpacing/100.f*FVector(1.f, 1.f, 1.f));
				}

			});

			TArray<int32> Indexes = NewElem.I_Mesh->AddInstances(InstanceT,true);

			if(!(Indexes.Num()!=NumI || UV.Num()!=NumI|| UV1.Num()!=NumI))
			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					TArray<float> CustomData;
					//Section Indentifier
					CustomData.Add(2.f);
					CustomData.Add(UV[k].X);
					CustomData.Add(UV[k].Y);
					CustomData.Add(UV1[k].X);
					CustomData.Add(UV1[k].Y);
					CustomData.Add(Vertices[k].Z);
					NewElem.I_Mesh->SetCustomData(Indexes[k], CustomData);
				}

			});
		}

		Vertices.Empty();
		Triangles.Empty();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,LocalM * 2 + 1, 2, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f)+ (LocalM *2 -1) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,2, LocalM * 2, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// topleft
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(3, Vertices, Triangles, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);

		if (NewElem.I_Mesh)
		{
			int NumI = Vertices.Num();
			TArray<FTransform> InstanceT;
			InstanceT.SetNum(NumI);
			float ScaleLocal = pow(2.0, -i + LOD_Num - 1);

			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					FTransform& T = InstanceT[k];
					T.SetLocation(Vertices[k]);
					T.SetRotation(FQuat::Identity);
					T.SetScale3D(NewElem.GridSpacing/100.f*FVector(1.f, 1.f, 1.f));
				}

			});

			TArray<int32> Indexes = NewElem.I_Mesh->AddInstances(InstanceT,true);

			if(!(Indexes.Num()!=NumI || UV.Num()!=NumI|| UV1.Num()!=NumI))
			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					TArray<float> CustomData;
					//Section Indentifier
					CustomData.Add(3.f);
					CustomData.Add(UV[k].X);
					CustomData.Add(UV[k].Y);
					CustomData.Add(UV1[k].X);
					CustomData.Add(UV1[k].Y);
					CustomData.Add(Vertices[k].Z);
					NewElem.I_Mesh->SetCustomData(Indexes[k], CustomData);
				}

			});
		}

		Vertices.Empty();
		Triangles.Empty();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,2, LocalM * 2 + 1, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (LocalM*2.0-1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f)+ (1.0) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,LocalM * 2, 2, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// botright
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(4, Vertices, Triangles, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);

		if (NewElem.I_Mesh)
		{
			int NumI = Vertices.Num();
			TArray<FTransform> InstanceT;
			InstanceT.SetNum(NumI);
			float ScaleLocal = pow(2.0, -i + LOD_Num - 1);

			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					FTransform& T = InstanceT[k];
					T.SetLocation(Vertices[k]);
					T.SetRotation(FQuat::Identity);
					T.SetScale3D(NewElem.GridSpacing/100.f*FVector(1.f, 1.f, 1.f));
				}

			});

			TArray<int32> Indexes = NewElem.I_Mesh->AddInstances(InstanceT,true);

			if(!(Indexes.Num()!=NumI || UV.Num()!=NumI|| UV1.Num()!=NumI))
			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					TArray<float> CustomData;
					//Section Indentifier
					CustomData.Add(4.f);
					CustomData.Add(UV[k].X);
					CustomData.Add(UV[k].Y);
					CustomData.Add(UV1[k].X);
					CustomData.Add(UV1[k].Y);
					CustomData.Add(Vertices[k].Z);
					NewElem.I_Mesh->SetCustomData(Indexes[k], CustomData);
				}

			});
		}

		Vertices.Empty();
		Triangles.Empty();
		UV.Empty();
		UV1.Empty();
		UV2.Empty();

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f)+ (LocalM * 2.0 - 1.0) * NewElem.GridSpacing * FVector(1.f, 0.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,2, LocalM * 2 + 1, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		LocalOffset = FVector(-LocalExtent, -LocalExtent, 0.f) + ((LocalM - 1)) * NewElem.GridSpacing * FVector(1.f, 1.f, 0.f) + (LocalM * 2.0 - 1.0) * NewElem.GridSpacing * FVector(0.f, 1.f, 0.f);
		if (NewElem.I_Mesh)
			StichingProfile = 0;
		CreateGridMeshWelded(LOD,LocalM * 2, 2, Triangles, Vertices, UV,UV1,UV2, NewElem.GridSpacing, LocalOffset,StichingProfile);

		Normals.Empty();
		Normals.Init(FVector(0.f, 0.f, 1.f), Vertices.Num());
		Tangents.Empty();
		Tangents.Init(FGeoCProcMeshTangent(FVector(0.f, 0.f, 1.f), false), Vertices.Num());
		
		// topright
		if(NewElem.Mesh)
		NewElem.Mesh->CreateMeshSection(5, Vertices, Triangles, Normals, UV, UV1, UV2, UV_dummy, Color_dummy, Tangents, false);


		if (NewElem.I_Mesh)
		{
			int NumI = Vertices.Num();
			TArray<FTransform> InstanceT;
			InstanceT.SetNum(NumI);
			float ScaleLocal = pow(2.0, -i + LOD_Num - 1);

			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					FTransform& T = InstanceT[k];
					T.SetLocation(Vertices[k]);
					T.SetRotation(FQuat::Identity);
					T.SetScale3D(NewElem.GridSpacing/100.f*FVector(1.f, 1.f, 1.f));
				}

			});

			TArray<int32> Indexes = NewElem.I_Mesh->AddInstances(InstanceT,true);

			if(!(Indexes.Num()!=NumI || UV.Num()!=NumI|| UV1.Num()!=NumI))
			ParallelFor(NumI, [&](int32 k)
			{
				if (k < NumI)
				{
					TArray<float> CustomData;
					//Section Indentifier
					CustomData.Add(5.f);
					CustomData.Add(UV[k].X);
					CustomData.Add(UV[k].Y);
					CustomData.Add(UV1[k].X);
					CustomData.Add(UV1[k].Y);
					CustomData.Add(Vertices[k].Z);
					NewElem.I_Mesh->SetCustomData(Indexes[k], CustomData);
				}

			});
		}


		LocalOffset = FVector(-LocalExtent,-LocalExtent,0.f);

		if(NewElem.SectionVisibility.Num()!=6)
		{
			NewElem.SectionVisibility.SetNum(6);
			for(bool& el : NewElem.SectionVisibility)
				el = true;
		}
			
		
		
		

		NewElem.Config= EClipMapInteriorConfig::BotLeft;

		if (EnableCaching && ComputeCacheMat)
		{
			NewElem.CacheMatDyn = UMaterialInstanceDynamic::Create(ComputeCacheMat, this);
#if !WITH_EDITOR
			NewElem.CacheMatDyn->AddToRoot();
#endif
			NewElem.CacheMatDyn->SetVectorParameterValue("RingLocation", NewElem.Location);
			NewElem.CacheMatDyn->SetScalarParameterValue("MeshScale", (N - 1) * NewElem.GridSpacing * (CacheRes<=1?1: CacheRes / (CacheRes - 1)));
			NewElem.CacheMatDyn->SetScalarParameterValue("N", N);
			NewElem.CacheMatDyn->SetScalarParameterValue("CacheRes", CacheRes);
			NewElem.CacheMatDyn->SetScalarParameterValue("LocalGridScaling", NewElem.GridSpacing);
			NewElem.CacheMatDyn->SetScalarParameterValue("NormalMapSelect", 0.f);
			UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.HeightMap, FLinearColor::Black);
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NewElem.HeightMap, NewElem.CacheMatDyn);
			NewElem.CacheMatDyn->SetScalarParameterValue("NormalMapSelect", 1.f);
			UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.NormalMap, FLinearColor::Black);
			UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NewElem.NormalMap, NewElem.CacheMatDyn);

			int layerIndice = 0;
			for(FClipMapLayer& Layer : LandDataLayers)
			{
				if(Layer.LayerName!="" && Layer.MaterialToGenerateLayer)
				{
					UMaterialInstanceDynamic* LayerDynMat = UMaterialInstanceDynamic::Create(Layer.MaterialToGenerateLayer, this);
#if !WITH_EDITOR
					LayerDynMat->AddToRoot();
#endif					

					// required for Position to UV coord
					LayerDynMat->SetVectorParameterValue("RingLocation", NewElem.Location);
					LayerDynMat->SetScalarParameterValue("MeshScale", (N - 1) * NewElem.GridSpacing * (CacheRes<=1?1: CacheRes / (CacheRes - 1)));
					LayerDynMat->SetScalarParameterValue("N", N);
					LayerDynMat->SetScalarParameterValue("CacheRes", CacheRes);
					LayerDynMat->SetScalarParameterValue("LocalGridScaling", NewElem.GridSpacing);

					LayerDynMat->SetTextureParameterValue("HeightMap", NewElem.HeightMap);
					LayerDynMat->SetTextureParameterValue("NormalMap", NewElem.NormalMap);

					for (int u = 0; u < layerIndice; u++)
					{
						LayerDynMat->SetTextureParameterValue(NewElem.LandLayers_names[u], NewElem.LandLayers[u]);
					}

					UKismetRenderingLibrary::ClearRenderTarget2D(this, NewElem.LandLayers[layerIndice], FLinearColor::Black);
					UKismetRenderingLibrary::DrawMaterialToRenderTarget(this, NewElem.LandLayers[layerIndice], LayerDynMat);

					NewElem.LayerMatDyn.Add(LayerDynMat);

					

					layerIndice++;
				}
			}
			
		}

		if( RuntimeMaterial)
		{
			if(NewElem.Mesh)
			{
				if (EnableCaching && CachedMaterial)
					NewElem.MatDyn = UMaterialInstanceDynamic::Create(CachedMaterial, this);
				else
					NewElem.MatDyn = UMaterialInstanceDynamic::Create(RuntimeMaterial, this);
			}
			else
			{
				if(Material_InstancedMeshRepresentation)
					NewElem.MatDyn = UMaterialInstanceDynamic::Create(Material_InstancedMeshRepresentation, this);
					
			}

#if !WITH_EDITOR
			if(NewElem.MatDyn)
				NewElem.MatDyn->AddToRoot();
#endif
			

			
		}

		if(NewElem.MatDyn)
		{
			if(NewElem.Mesh)
			{
				NewElem.Mesh->SetMaterial(0, NewElem.MatDyn);
				NewElem.Mesh->SetMaterial(1, NewElem.MatDyn);
				NewElem.Mesh->SetMaterial(2, NewElem.MatDyn);
				NewElem.Mesh->SetMaterial(3, NewElem.MatDyn);
				NewElem.Mesh->SetMaterial(4, NewElem.MatDyn);
				NewElem.Mesh->SetMaterial(5, NewElem.MatDyn);
			}
			if(NewElem.I_Mesh)
				NewElem.I_Mesh->SetMaterial(0, NewElem.MatDyn);
			

			NewElem.MatDyn->SetVectorParameterValue("RingLocation", NewElem.Location);
			NewElem.MatDyn->SetScalarParameterValue("MeshScale", (N - 1) * NewElem.GridSpacing * (CacheRes<=1?1: CacheRes / (CacheRes - 1)));
			NewElem.MatDyn->SetScalarParameterValue("N", N);
			NewElem.MatDyn->SetScalarParameterValue("LocalGridScaling", NewElem.GridSpacing);
			NewElem.MatDyn->SetScalarParameterValue("CacheRes", CacheRes);

			NewElem.MatDyn->SetTextureParameterValue("HeightMap", NewElem.HeightMap);
			NewElem.MatDyn->SetTextureParameterValue("NormalMap", NewElem.NormalMap);

			for (int u = 0; u < NewElem.LandLayers.Num(); u++)
			{
				NewElem.MatDyn->SetTextureParameterValue(NewElem.LandLayers_names[u], NewElem.LandLayers[u]);
			}
		}


		if (i == LOD_Num - 1)
			NewElem.SetSectionVisible(1, false);
		else
			NewElem.SetSectionVisible(0, false);

		// botleft
		NewElem.SetSectionVisible(2, true);
		// topleft
		NewElem.SetSectionVisible(3, false);
		// botright
		NewElem.SetSectionVisible(4, false);
		// topright
		NewElem.SetSectionVisible(5, false);
		
		

		Meshes.Add(NewElem);

	}
	
	if (DataReceiver)
		DataReceiver->UpdateStaticDataFor(this);
}

void AGeometryClipMapWorld::Merge_SortList(TArray<int>& SourceList)
{
	if (SourceList.Num() == 0 || SourceList.Num() == 1)
		return;


	int MidIndex = SourceList.Num()/2;

	TArray<int> UnSortedList_Left;

	TArray<int> UnSortedList_Right;

	for (int i = 0; i < SourceList.Num(); i++)
	{
		if (i < MidIndex)
			UnSortedList_Left.Add(SourceList[i]);
		else
			UnSortedList_Right.Add(SourceList[i]);
	}

	Merge_SortList(UnSortedList_Left);	
	Merge_SortList(UnSortedList_Right);

	TArray<int> Sorted_List;
	Sorted_List.Empty();

	while(UnSortedList_Left.Num()>0 && UnSortedList_Right.Num()>0)
	{
		if(Spawnables[UnSortedList_Left[0]].RegionWorldDimension<Spawnables[UnSortedList_Right[0]].RegionWorldDimension)
		{
			Sorted_List.Add(UnSortedList_Right[0]);
			UnSortedList_Right.RemoveAt(0);			
		}
		else
		{
			Sorted_List.Add(UnSortedList_Left[0]);
			UnSortedList_Left.RemoveAt(0);
		}
	}

	if(UnSortedList_Left.Num()>0)
		Sorted_List.Append(UnSortedList_Left);

	UnSortedList_Left.Empty();

	if (UnSortedList_Right.Num() > 0)
		Sorted_List.Append(UnSortedList_Right);

	UnSortedList_Right.Empty();

	for (int i = 0; i < SourceList.Num(); i++)
	{
		SourceList[i] = Sorted_List[i];
	}

}


void AGeometryClipMapWorld::SortSpawnabledBySurface()
{
	SortedSpawnables.Empty();

	if (Spawnables.Num() == 1)
	{
		SortedSpawnables.Add(0);
		return;
	}

	for (int i = 0; i < Spawnables.Num(); i++)
	{		
		SortedSpawnables.Add(i);
	}
	
	Merge_SortList(SortedSpawnables);	
	
}

bool AGeometryClipMapWorld::UpdateSpawnable(int indice, bool MustBeInFrustum)
{
	FSpawnableMesh& Spawn = Spawnables[indice];



	if (Spawn.SpawnType == ESpawnableType::Mesh && (Spawn.Mesh.Num() == 0 || Spawn.Mesh.Num() > 0 && !Spawn.Mesh[0]) ||
		Spawn.SpawnType == ESpawnableType::Actor && (Spawn.Actors.Num() == 0 || Spawn.Actors.Num() > 0 && !(*Spawn.Actors[0])))
		return true;

			
	if (!Spawn.Owner)
		Spawn.Initiate(this);

		FVector CompLoc = CamLocation / (Spawn.RegionWorldDimension);
		int CamX = FMath::RoundToInt(CompLoc.X);
		int CamY = FMath::RoundToInt(CompLoc.Y);

	//Only necessary to do this once during the initial InFrustum pass
	if (MustBeInFrustum)
	{

		FVector LocRef = Spawn.RegionWorldDimension * FVector(CamX, CamY, 0.f) + GetActorLocation().Z * FVector(0.f, 0.f, 1);

		for (int i = Spawn.UsedSpawnablesElem.Num() - 1; i >= 0; i--)
		{
			
			FSpawnableMeshElement& El = Spawn.SpawnablesElem[Spawn.UsedSpawnablesElem[i]];
			FVector ToComp = (El.Location - LocRef) / Spawn.RegionWorldDimension;

			if (FMath::Abs(ToComp.X) > Spawn.NumberRegionPerQuadrantSide+0.1f || FMath::Abs(ToComp.Y) > Spawn.NumberRegionPerQuadrantSide+0.1f)
			{
				Spawn.AvailableSpawnablesElem.Add(El.ID);
				Spawn.UsedSpawnablesElem.RemoveAt(i);
				El.ComputeLaunched = true;

				for (auto It = Spawn.SpawnablesLayout.CreateConstIterator(); It; ++It)
				{
					if (It->Value == El.ID)
					{
						Spawn.SpawnablesLayout.Remove(It->Key);
						break;
					}
				}

			}

		}

	}

	bool InterruptUpdate = false;

	if (Spawn.IndexOfClipMapForCompute > 0 && Spawn.IndexOfClipMapForCompute < GetMeshNum())
	{
		FClipMapMeshElement& Elem = GetMesh(Spawn.IndexOfClipMapForCompute);

		if (!Elem.IsSectionVisible(0) && !Elem.IsSectionVisible(1))
			return true;
	}

	for (int i = -Spawn.NumberRegionPerQuadrantSide; i <= Spawn.NumberRegionPerQuadrantSide; i++)
	{
		if (InterruptUpdate)
			break;

		for (int j = -Spawn.NumberRegionPerQuadrantSide; j <= Spawn.NumberRegionPerQuadrantSide; j++)
		{

			FIntVector LocMeshInt = FIntVector(CamX + i, CamY + j, 0);

			FVector MeshLoc = Spawn.RegionWorldDimension * FVector(LocMeshInt) + GetActorLocation().Z * FVector(0.f, 0.f, 1);


			if (!Spawn.SpawnablesLayout.Contains(LocMeshInt))
			{
				if (!MustBeInFrustum || MustBeInFrustum && (ViewFrustum.Planes.Num() == 0 || ViewFrustum.IntersectBox(MeshLoc, Spawn.ExtentOfMeshElement)))
				{
					if (CanUpdateSpawnables())
					{
						FSpawnableMeshElement& Mesh = Spawn.GetASpawnableElem();						

						Mesh.Location = MeshLoc;

						Spawn.UpdateSpawnableData(Mesh);

						Spawn.SpawnablesLayout.Add(LocMeshInt, Mesh.ID);
					}
					else
					{
						InterruptUpdate = true;
						break;

					}
				}


			}

		}

	}

	if (InterruptUpdate)
	{		
		return false;
	}
	return true;
}

void AGeometryClipMapWorld::UpdateSpawnables()
{

	if(SortedSpawnables.Num()==0 && Spawnables.Num()>0)
		SortSpawnabledBySurface();

	bool Interrupted = false;

	//Update spawnables in Frustum
	for (int& indice : SortedSpawnables)		
	{
		if(!UpdateSpawnable(indice,true))
		{
			Interrupted=true;
			break;
		}

	}

	// If we were not interrupted then we have remaining draw call budget update spawnables out of frustum	
	if(!Interrupted)
	{	
		for (int& indice : SortedSpawnables)
		{
			if (!UpdateSpawnable(indice, false))							
				break;			
		}
	
	}

}


FSpawnableMeshElement& FSpawnableMesh::GetASpawnableElem()
{

		

	if (AvailableSpawnablesElem.Num() > 0)
	{
		FSpawnableMeshElement& Elem = SpawnablesElem[AvailableSpawnablesElem[AvailableSpawnablesElem.Num() - 1]];
		UsedSpawnablesElem.Add(Elem.ID);
		AvailableSpawnablesElem.RemoveAt(AvailableSpawnablesElem.Num() - 1);
		return Elem;
	}

	//if the spawnable is not defined yet, early out
	if( SpawnType == ESpawnableType::Mesh && (Mesh.Num()==0 || Mesh.Num()>0 && !Mesh[0]) ||
		SpawnType == ESpawnableType::Actor && (Actors.Num()==0 || Actors.Num()>0 && !(*Actors[0])))
	{
		FSpawnableMeshElement NewElem;
		SpawnablesElem.Add(NewElem);
		return SpawnablesElem[SpawnablesElem.Num() - 1];
	}
		

	if (!Owner)
	{
		UE_LOG(LogTemp,Warning,TEXT("ERROR : FSpawnableMesh has no owner"));
		FSpawnableMeshElement NewElem;
		SpawnablesElem.Add(NewElem);
		return SpawnablesElem[SpawnablesElem.Num() - 1];	
	}


	FSpawnableMeshElement NewElem;
	NewElem.ID = SpawnablesElem.Num();
	
	UWorld* World = Owner->GetWorld();

	uint32 SizeT = (uint32)RT_Dim;

	Owner->RendertargetMemoryBudgetMB+=4*(SizeT*SizeT*4)/1000000.0f;

	NewElem.LocationX = UKismetRenderingLibrary::CreateRenderTarget2D(World, SizeT, SizeT, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
	NewElem.LocationX->AddToRoot();
#endif
	NewElem.LocationX->UpdateResourceImmediate();
	NewElem.LocationY = UKismetRenderingLibrary::CreateRenderTarget2D(World, SizeT, SizeT, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
	NewElem.LocationY->AddToRoot();
#endif
	NewElem.LocationY->UpdateResourceImmediate();
	NewElem.LocationZ = UKismetRenderingLibrary::CreateRenderTarget2D(World, SizeT, SizeT, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
	NewElem.LocationZ->AddToRoot();
#endif
	NewElem.LocationZ->UpdateResourceImmediate();

	NewElem.Rotation = UKismetRenderingLibrary::CreateRenderTarget2D(World, SizeT, SizeT, RTF_RGBA8, FLinearColor(0, 0, 0, 1), false);
#if !WITH_EDITOR
	NewElem.Rotation->AddToRoot();
#endif
	NewElem.Rotation->UpdateResourceImmediate();


	UsedSpawnablesElem.Add(NewElem.ID);
	SpawnablesElem.Add(NewElem);

	return SpawnablesElem[SpawnablesElem.Num() - 1];
	
}

void FSpawnableMesh::ReleaseSpawnableElem(int ID)
{
	AvailableSpawnablesElem.Add(ID);

}

void ReadPixelsFromRT_Spawn(FSpawnableMeshElement& Mesh)
{
	ENQUEUE_RENDER_COMMAND(ReadGeoClipMapRTCmd)(
		[&Mesh](FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());

		Mesh.LocationXData.SetNum(Mesh.LocationX->GetRenderTargetResource()->GetSizeX() * Mesh.LocationX->GetRenderTargetResource()->GetSizeX(), false);
		RHICmdList.ReadSurfaceData(
			Mesh.LocationX->GetRenderTargetResource()->GetRenderTargetTexture(),
			FIntRect(0, 0, Mesh.LocationX->GetRenderTargetResource()->GetSizeX(), Mesh.LocationX->GetRenderTargetResource()->GetSizeY()),
			Mesh.LocationXData,
			FReadSurfaceDataFlags());

		Mesh.LocationYData.SetNum(Mesh.LocationY->GetRenderTargetResource()->GetSizeX() * Mesh.LocationY->GetRenderTargetResource()->GetSizeX(), false);
		RHICmdList.ReadSurfaceData(
			Mesh.LocationY->GetRenderTargetResource()->GetRenderTargetTexture(),
			FIntRect(0, 0, Mesh.LocationY->GetRenderTargetResource()->GetSizeX(), Mesh.LocationY->GetRenderTargetResource()->GetSizeY()),
			Mesh.LocationYData,
			FReadSurfaceDataFlags());

		Mesh.LocationZData.SetNum(Mesh.LocationZ->GetRenderTargetResource()->GetSizeX() * Mesh.LocationZ->GetRenderTargetResource()->GetSizeX(), false);
		RHICmdList.ReadSurfaceData(
			Mesh.LocationZ->GetRenderTargetResource()->GetRenderTargetTexture(),
			FIntRect(0, 0, Mesh.LocationZ->GetRenderTargetResource()->GetSizeX(), Mesh.LocationZ->GetRenderTargetResource()->GetSizeY()),
			Mesh.LocationZData,
			FReadSurfaceDataFlags());

		Mesh.RotationData.SetNum(Mesh.Rotation->GetRenderTargetResource()->GetSizeX() * Mesh.Rotation->GetRenderTargetResource()->GetSizeX(), false);
		RHICmdList.ReadSurfaceData(
			Mesh.Rotation->GetRenderTargetResource()->GetRenderTargetTexture(),
			FIntRect(0, 0, Mesh.Rotation->GetRenderTargetResource()->GetSizeX(), Mesh.Rotation->GetRenderTargetResource()->GetSizeY()),
			Mesh.RotationData,
			FReadSurfaceDataFlags());
	});
	FlushRenderingCommands();
}

void FSpawnableMesh::UpdateSpawnableData(FSpawnableMeshElement& MeshElem)
{
	FVector MesgLoc = MeshElem.Location;


	if (Owner && Owner->SpawnablesMat)
	{

		//OPTION A : Compute collision form GPU readback

		UMaterialInstanceDynamic* DynSpawnMat = MeshElem.ComputeSpawnTransformDyn ? MeshElem.ComputeSpawnTransformDyn : (UMaterialInstanceDynamic::Create(CustomSpawnablesMat?CustomSpawnablesMat:Owner->SpawnablesMat, Owner));
		
		 

		//Prevent recompute by reading HeightMap and NormalMap
		//would need a different material to switch
		if(IndexOfClipMapForCompute>0 && IndexOfClipMapForCompute<Owner->GetMeshNum())
		{		
			FClipMapMeshElement& Elem = Owner->GetMesh(IndexOfClipMapForCompute);

			if (!MeshElem.ComputeSpawnTransformDyn)
			{
#if !WITH_EDITOR
				DynSpawnMat->AddToRoot();
#endif

				MeshElem.ComputeSpawnTransformDyn = DynSpawnMat;
				DynSpawnMat->SetScalarParameterValue("N", Owner->N);
				DynSpawnMat->SetScalarParameterValue("LocalGridScaling", Elem.GridSpacing);
				DynSpawnMat->SetTextureParameterValue("HeightMap", Elem.HeightMap);
				DynSpawnMat->SetTextureParameterValue("NormalMap", Elem.NormalMap);
				for(int i=0; i<Elem.LandLayers.Num();i++)
				{
					DynSpawnMat->SetTextureParameterValue(Elem.LandLayers_names[i], Elem.LandLayers[i]);
				}
			}

			if(Elem.IsSectionVisible(0)||Elem.IsSectionVisible(1))
			{
				DynSpawnMat->SetScalarParameterValue("ComputeToggle", 0.f);
				DynSpawnMat->SetVectorParameterValue("RingLocation", Elem.Location);
	
			}
			else
				return;

		}
		else
		{
			DynSpawnMat->SetScalarParameterValue("ComputeToggle", 1.f);
		}

		//

		DynSpawnMat->SetVectorParameterValue("MeshLocation", MesgLoc);
		DynSpawnMat->SetScalarParameterValue("MeshScale", RegionWorldDimension * (RT_Dim<=1? 1 : RT_Dim / (RT_Dim - 1)));

		DynSpawnMat->SetScalarParameterValue("RT_Dim", RT_Dim);
		DynSpawnMat->SetScalarParameterValue("OutputRotationScale", 0.f);

		DynSpawnMat->SetScalarParameterValue("AlignMaxAngle", AlignMaxAngle);
		DynSpawnMat->SetScalarParameterValue("MinSpawnHeight", AltitudeRange.Min);
		DynSpawnMat->SetScalarParameterValue("MaxSpawnHeight", AltitudeRange.Max);

		DynSpawnMat->SetScalarParameterValue("MinScale", ScaleRange.Min);
		DynSpawnMat->SetScalarParameterValue("MaxScale", ScaleRange.Max);

		DynSpawnMat->SetScalarParameterValue("MinGroundSlope", GroundSlopeAngle.Min);
		DynSpawnMat->SetScalarParameterValue("MaxGroundSlope", GroundSlopeAngle.Max);
		
		
		// 0.f = X
		// 2.f = Y
		// 4.f = Z
		// 6.f = Rotation
		float IndexOutput = 0.f;
		DynSpawnMat->SetScalarParameterValue("OutputIndex", IndexOutput);
		UKismetRenderingLibrary::ClearRenderTarget2D(Owner, MeshElem.LocationX, FLinearColor::Black);	
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(Owner, MeshElem.LocationX, DynSpawnMat);
		
		MeshElem.LocationXData.Empty();
		MeshElem.LocationXData.SetNum(RT_Dim * RT_Dim, false);

		IndexOutput = 2.f;
		DynSpawnMat->SetScalarParameterValue("OutputIndex", IndexOutput);
		UKismetRenderingLibrary::ClearRenderTarget2D(Owner, MeshElem.LocationY, FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(Owner, MeshElem.LocationY, DynSpawnMat);
		
		MeshElem.LocationYData.Empty();
		MeshElem.LocationYData.SetNum(RT_Dim * RT_Dim, false);

		IndexOutput = 4.f;
		DynSpawnMat->SetScalarParameterValue("OutputIndex", IndexOutput);
		UKismetRenderingLibrary::ClearRenderTarget2D(Owner, MeshElem.LocationZ, FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(Owner, MeshElem.LocationZ, DynSpawnMat);
		
		MeshElem.LocationZData.Empty();
		MeshElem.LocationZData.SetNum(RT_Dim * RT_Dim, false);

		IndexOutput = 6.f;
		DynSpawnMat->SetScalarParameterValue("OutputIndex", IndexOutput);
		DynSpawnMat->SetScalarParameterValue("OutputRotationScale", 1.f);
		UKismetRenderingLibrary::ClearRenderTarget2D(Owner, MeshElem.Rotation, FLinearColor::Black);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(Owner, MeshElem.Rotation, DynSpawnMat);
		
		MeshElem.RotationData.Empty();
		MeshElem.RotationData.SetNum(RT_Dim * RT_Dim, false);
		
		ReadPixelsFromRT_Spawn(MeshElem);
		SpawnablesElemReadToProcess.Add(MeshElem.ID);
		

		return;
	}

	return;
}

void FSpawnableMesh::Initiate(AGeometryClipMapWorld* Owner_)
{
	CleanUp();	
	
	if(Owner_ && ((SpawnType==ESpawnableType::Mesh && Mesh.Num()>0 && Mesh[0] && HIM_Mesh.Num()==0) 
				||(SpawnType==ESpawnableType::Actor && Actors.Num()>0 && (*Actors[0])!= nullptr && Spawned_Actors.Num()==0)))
	{
		Owner=Owner_;



		/// Computation Optimization
		/// instead of evaluating the noise and generating the precise normal, we can use the already computed heightmap/NormalMap
		/// it adds a dependency to those maps/this ring but makes for far better performances
		
		for (int i = Owner->GetMeshNum()-1; i >=0 ; i--)
		{
			FClipMapMeshElement& Elem = Owner->GetMesh(i);
			
			if(Elem.GridSpacing*(Owner->N-1)/2.f>RegionWorldDimension*(4.f))
			{				
				IndexOfClipMapForCompute=i;
				break;
			}
		}
		

		int PoolItemCount=0;
		NumInstancePerHIM.Empty();
		if(SpawnType==ESpawnableType::Mesh)
			for (UStaticMesh* Sm : Mesh)
			{
				if (Sm)
				{
					PoolItemCount++;
					NumInstancePerHIM.Add(0);
				}

			}
		else
			for (TSubclassOf<AActor>& A : Actors)
			{
				if ((*A)!= nullptr)
				{
					PoolItemCount++;
					NumInstancePerHIM.Add(0);
				}

			}


		if (NumberOfInstanceToComputePerRegion < PoolItemCount)
			NumberOfInstanceToComputePerRegion = PoolItemCount;
		
		RT_Dim = FMath::Floor(FMath::Sqrt((float)NumberOfInstanceToComputePerRegion)) + (FMath::Frac(FMath::Sqrt((float)NumberOfInstanceToComputePerRegion))>0.f?1:0);

		// Compute grid real world size is : RegionWorldDimension * RT_Dim / (RT_Dim - 1)
		// To reach top right corner from center of grid we using half this distance in X Y. 
		// Adding a large Z value to counter our lack of information about the height assets will spawn at
		
		ExtentOfMeshElement = FVector(1.f,1.f,0.f)*(RegionWorldDimension * (RT_Dim<=1? 1 : RT_Dim / (RT_Dim - 1)))/2.f + FVector(0.f,0.f,1.f)*(Owner->VerticalRangeMeters*100.f);


		const int NumOfVertex = RT_Dim*RT_Dim;

		for(int i=0; i<NumOfVertex;i++)
		{
			InstanceIndexToHIMIndex.Add(i%PoolItemCount);
			InstanceIndexToIndexForHIM.Add(NumInstancePerHIM[i%PoolItemCount]);
			NumInstancePerHIM[i%PoolItemCount] = NumInstancePerHIM[i%PoolItemCount] + 1;
			
		}

		int PoolTargetIncrement=0;
		if(SpawnType==ESpawnableType::Mesh)
		for(UStaticMesh* Sm : Mesh)
		{
			if(Sm)
			{
				UHierarchicalInstancedStaticMeshComponent* NHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
#if !WITH_EDITOR
				NHISM->AddToRoot();
#endif
				NHISM->SetupAttachment(Owner->GetRootComponent());
				NHISM->RegisterComponent();
				NHISM->SetStaticMesh(Sm);
				NHISM->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
				NHISM->SetCollisionEnabled(CollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
				NHISM->SetCastShadow(CastShadows);
				HIM_Mesh.Add(NHISM);
			}
		}
		else
			for (TSubclassOf<AActor>& A : Actors)
			{
				if ((*A) != nullptr)
				{
					Spawned_Actors.Add(FSpawnedActorList());
					Actors_Validated.Add(A);
				}

			}

	}
}

void FSpawnableMesh::CleanUp()
{
	
	for(FSpawnableMeshElement& El:SpawnablesElem)
	{
		if(El.LocationX && Owner)
			Owner->RendertargetMemoryBudgetMB-=4*(El.LocationX->SizeX *El.LocationX->SizeX*4)/1000000.0f;

		if (El.LocationX->IsRooted())
			El.LocationX->RemoveFromRoot();
		if (El.LocationY->IsRooted())
			El.LocationY->RemoveFromRoot();
		if (El.LocationZ->IsRooted())
			El.LocationZ->RemoveFromRoot();
		if (El.Rotation->IsRooted())
			El.Rotation->RemoveFromRoot();
		if (El.ComputeSpawnTransformDyn && El.ComputeSpawnTransformDyn->IsRooted())
			El.ComputeSpawnTransformDyn->RemoveFromRoot();

		
		El.LocationX=nullptr;
		El.LocationY=nullptr;
		El.LocationZ=nullptr;
		El.Rotation=nullptr;
		El.ComputeSpawnTransformDyn=nullptr;
	}

	SpawnablesElem.Empty();
	AvailableSpawnablesElem.Empty();
	UsedSpawnablesElem.Empty();	
	SpawnablesElemReadToProcess.Empty();

	SpawnablesLayout.Empty();

	for (UHierarchicalInstancedStaticMeshComponent* HISM : HIM_Mesh)
	{
		if(HISM && Owner && Owner->GetWorld())
		{
			if (HISM->IsRooted())
				HISM->RemoveFromRoot();
			HISM->ClearInstances();
			HISM->UnregisterComponent();
			HISM->DestroyComponent();
			HISM = nullptr;
		}		
	}
	HIM_Mesh.Empty();

	UWorld* World = nullptr;
	if(Owner)
		World = Owner->GetWorld();

	for (FSpawnedActorList& SL: Spawned_Actors)
	{
		for (AActor* Act: SL.SpawnedActors)
		{
			if(World && Act)
				World->DestroyActor(Act); 
		}
		SL.SpawnedActors.Empty();
	}
	Spawned_Actors.Empty();
	Actors_Validated.Empty();

	if (SpawnType == ESpawnableType::Mesh)
		Actors.Empty();
	else	
		Mesh.Empty();


	InstanceIndexToHIMIndex.Empty();
	NumInstancePerHIM.Empty();
	InstanceIndexToIndexForHIM.Empty();

	

	IndexOfClipMapForCompute=-1;


	Owner = nullptr;
	
}

FSpawnableMesh::~FSpawnableMesh()
{
	CleanUp();
}

bool FClipMapMeshElement::IsSectionVisible(int SectionID)
{
	if(SectionID>=0 && SectionID<SectionVisibility.Num())
	{
		return SectionVisibility[SectionID];
	}
	return false;
}

void FClipMapMeshElement::SetSectionVisible(int SectionID,bool NewVisibility)
{

	if (SectionID >= 0 && SectionID < SectionVisibility.Num())
	{
		SectionVisibility[SectionID]=NewVisibility;

		if(Mesh)
			Mesh->SetMeshSectionVisible(SectionID,NewVisibility);
		
		if(I_Mesh)
		{
			if(!SectionVisibility[0] && !SectionVisibility[1] && I_Mesh->IsVisible())
			{
				I_Mesh->SetVisibility(false);
			}
			if ((SectionVisibility[0] || SectionVisibility[1]) && !I_Mesh->IsVisible())
			{
				I_Mesh->SetVisibility(true);
			}

			if(MatDyn && NewVisibility)
			{
				if(SectionID<=1)
					MatDyn->SetScalarParameterValue("Ring_Main_Profile",SectionID);
				else
					MatDyn->SetScalarParameterValue("Ring_Second_Profile",SectionID);

			}
		}
		
		
	}
	
}
