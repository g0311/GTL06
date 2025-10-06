#pragma once
#include "Object.h"
#include "UEContainer.h"

// Forward Declarations
class AActor;
class USceneComponent;

/**
 * SelectionManager
 * - 액터 선택 상태를 관리하는 싱글톤 클래스
 * UGameInstanceSubsystem 써야함
 */
class USelectionManager : public UObject
{
public:
    DECLARE_CLASS(USelectionManager, UObject)

    /** === 액터 선택 관리 === */
    void SelectActor(AActor* Actor);
    void DeselectActor(AActor* Actor);
    void ClearSelection();
    
    bool IsActorSelected(AActor* Actor) const;
    
    /** === 컴포넌트 선택 관리 === */
    void SelectComponent(USceneComponent* Component);
    void DeselectComponent();
    void ClearComponentSelection();
    
    bool IsComponentSelected(USceneComponent* Component) const;
    bool HasComponentSelection() const { return SelectedComponent != nullptr; }
    
    /** === 선택된 액터 접근 === */
    AActor* GetSelectedActor() const; // 단일 선택용
    const TArray<AActor*>& GetSelectedActors() const { return SelectedActors; }
    
    int32 GetSelectionCount() const { return SelectedActors.Num(); }
    bool HasSelection() const { return SelectedActors.Num() > 0; }
    
    /** === 선택된 컴포넌트 접근 === */
    USceneComponent* GetSelectedComponent() const { return SelectedComponent; }
    
    /** === 통합 선택 정보 === */
    // 컴포넌트가 선택되어 있으면 컴포넌트의 위치, 그렇지 않으면 액터의 위치 반환
    FVector GetSelectionLocation() const;
    FQuat GetSelectionRotation() const;
    FVector GetSelectionScale() const;
    FTransform GetSelectionTransform() const;
    
    /** === 삭제된 액터/컴포넌트 정리 === */
    void CleanupInvalidActors(); // null이나 삭제된 액터 제거
    void CleanupInvalidComponents(); // null이나 삭제된 컴포넌트 제거

public:
    USelectionManager();
    ~USelectionManager() override;
protected:
    
    // 복사 금지
    USelectionManager(const USelectionManager&) = delete;
    USelectionManager& operator=(const USelectionManager&) = delete;
    
    /** === 선택된 액터들 === */
    TArray<AActor*> SelectedActors;
    
    /** === 선택된 컴포넌트 === */
    USceneComponent* SelectedComponent = nullptr;
};
