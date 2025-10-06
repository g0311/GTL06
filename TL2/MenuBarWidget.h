#pragma once
#include "UI/Widget/Widget.h"
class USlateManager;

/**
 * 상단 메인 메뉴바 위젯 (File / Edit / Window / Help)
 * - 오너(USlateManager)를 주입받아 레이아웃 스위칭 등 제어
 * - 필요 시 외부 콜백으로 기본 동작 오버라이드 가능
 */
class UMenuBarWidget : public UWidget
{
public:
    DECLARE_CLASS(UMenuBarWidget, UWidget)

    UMenuBarWidget();
    explicit UMenuBarWidget(USlateManager* InOwner);

    // UWidget 인터페이스
    void Initialize() override;
    void Update() override;
    void RenderWidget() override;

    // 오너 설정
    void SetOwner(USlateManager* InOwner) { Owner = InOwner; }

    // 선택: File 액션 콜백 핸들러 (단순화된 UI에서는 File만 사용)
    void SetFileActionHandler(std::function<void(const char*)> Fn) { FileAction = std::move(Fn); }

private:
    // File 메뉴 동작
    void OnFileMenuAction(const char* action);
    
    // ShowFlag 메뉴 렌더링
    void RenderShowFlagsMenu();
    void RenderShowFlagMenuItem(const char* Label, uint64_t Flag);
    
    
    // 파일 다이얼로그 헬퍼
    std::string ShowLoadFileDialog();
    std::string ShowSaveFileDialog();

private:
    USlateManager* Owner = nullptr;

    // File 액션을 위한 선택적 콜백
    std::function<void(const char*)> FileAction;
    
};
