#include "pch.h"
#include "SelectionManager.h"
#include "Actor.h"
#include "SceneComponent.h"

void USelectionManager::SelectActor(AActor* Actor)
{
    if (!Actor) return;
    
    // 이미 선택되어 있는지 확인
    if (IsActorSelected(Actor)) return;
    
    // 단일 선택 모드 (기존 선택 해제)
    ClearSelection();
    
    // 컴포넌트 선택도 해제
    ClearComponentSelection();
    
    // 새 액터 선택
    SelectedActors.Add(Actor);
}

void USelectionManager::DeselectActor(AActor* Actor)
{
    if (!Actor) return;
    
    auto it = std::find(SelectedActors.begin(), SelectedActors.end(), Actor);
    if (it != SelectedActors.end())
    {
        SelectedActors.erase(it);
    }
}

void USelectionManager::ClearSelection()
{
    for (AActor* Actor : SelectedActors)
    {
        if (Actor) // null 체크 추가
        {
            Actor->SetIsPicked(false);
        }
    }
    SelectedActors.clear();
    
    // 컴포넌트 선택도 함께 해제
    ClearComponentSelection();
}

bool USelectionManager::IsActorSelected(AActor* Actor) const
{
    if (!Actor) return false;
    
    return std::find(SelectedActors.begin(), SelectedActors.end(), Actor) != SelectedActors.end();
}

AActor* USelectionManager::GetSelectedActor() const
{
    // 첫 번째 유효한 액터 연기
    for (AActor* Actor : SelectedActors)
    {
        if (Actor) return Actor;
    }
    return nullptr;
}

void USelectionManager::CleanupInvalidActors()
{
    // null이거나 삭제된 액터들을 제거
    auto it = std::remove_if(SelectedActors.begin(), SelectedActors.end(), 
        [](AActor* Actor) { return Actor == nullptr; });
    SelectedActors.erase(it, SelectedActors.end());
}

USelectionManager::USelectionManager()
{
    SelectedActors.Reserve(1);
}

USelectionManager::~USelectionManager()
{
    // No-op: instances are destroyed by ObjectFactory::DeleteAll
}

// === 컴포넌트 선택 기능 구현 ===
void USelectionManager::SelectComponent(USceneComponent* Component)
{
    if (!Component) return;
    
    // 이미 선택되어 있는지 확인
    if (IsComponentSelected(Component)) return;
    
    // 액터 선택 해제 (컴포넌트 선택 시)
    ClearSelection();
    
    // 컴포넌트 선택
    SelectedComponent = Component;
    
    // 컴포넌트의 소유 액터도 선택 상태로 설정 (기즈모 표시를 위해)
    if (AActor* Owner = Component->GetOwner())
    {
        SelectedActors.Add(Owner);
        Owner->SetIsPicked(true);
    }
}

void USelectionManager::DeselectComponent()
{
    ClearComponentSelection();
}

void USelectionManager::ClearComponentSelection()
{
    SelectedComponent = nullptr;
}

bool USelectionManager::IsComponentSelected(USceneComponent* Component) const
{
    return SelectedComponent == Component;
}

// === 통합 선택 정보 ===
FVector USelectionManager::GetSelectionLocation() const
{
    if (SelectedComponent)
    {
        return SelectedComponent->GetWorldLocation();
    }
    else if (AActor* Actor = GetSelectedActor())
    {
        return Actor->GetActorLocation();
    }
    return FVector();
}

FQuat USelectionManager::GetSelectionRotation() const
{
    if (SelectedComponent)
    {
        return SelectedComponent->GetWorldRotation();
    }
    else if (AActor* Actor = GetSelectedActor())
    {
        return Actor->GetActorRotation();
    }
    return FQuat::Identity();
}

FVector USelectionManager::GetSelectionScale() const
{
    if (SelectedComponent)
    {
        return SelectedComponent->GetWorldScale();
    }
    else if (AActor* Actor = GetSelectedActor())
    {
        return Actor->GetActorScale();
    }
    return FVector(1.f,1.f,1.f);
}

FTransform USelectionManager::GetSelectionTransform() const
{
    if (SelectedComponent)
    {
        return SelectedComponent->GetWorldTransform();
    }
    else if (AActor* Actor = GetSelectedActor())
    {
        return Actor->GetActorTransform();
    }
    return FTransform();
}

void USelectionManager::CleanupInvalidComponents()
{
    // 간단한 null 체크만 수행 (또는 추후 Owner 체크 등을 추가 가능)
    if (!SelectedComponent)
    {
        return;
    }
    
    // 컴포넌트의 Owner가 유효한지 확인
    if (!SelectedComponent->GetOwner())
    {
        SelectedComponent = nullptr;
    }
}
