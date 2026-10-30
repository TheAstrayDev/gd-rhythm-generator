#pragma once

#include <Geode/Geode.hpp>

#include "Generator.hpp"

#include <atomic>
#include <memory>

// Modal window built only from cocos2d and GD classes. Geode's own UI classes (Popup,
// TextInput, Notification) are not used: on GD 2.2091 this mod is compiled against the
// SDK's v6 headers while the installed loader is 5.11.2, and constructing geode::Popup
// across that mismatch crashes the game.
class GeneratorPopup : public cocos2d::CCLayerColor, public CustomSongDelegate {
public:
    static GeneratorPopup* create(LevelEditorLayer* editor);
    void show();

    // CustomSongDelegate (GD's native song menu: Newgrounds, Music Library, NCS, ID field)
    void songIDChanged(int id) override;
    int getActiveSongID() override;
    std::string getSongFileName() override;
    LevelSettingsObject* getLevelSettings() override;

protected:
    ~GeneratorPopup() override;
    bool init(LevelEditorLayer* editor);

    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyBackClicked() override;

    void onClose(cocos2d::CCObject*);
    void onLibrary(cocos2d::CCObject*);
    void onDifficulty(cocos2d::CCObject* sender);
    void onToggleClear(cocos2d::CCObject*);
    void onToggleEffects(cocos2d::CCObject*);
    void onReroll(cocos2d::CCObject*);
    void onOffset(cocos2d::CCObject* sender);
    void onGenerate(cocos2d::CCObject*);
    void onStyles(cocos2d::CCObject*);
    void onStyleToggle(cocos2d::CCObject* sender);
    void onStylesDone(cocos2d::CCObject*);

    void checkDownload(float);
    void startDecode(float);
    void checkWorker(float);
    void checkSongLayer(float);
    void applyResult(rg::GenResult const& res);

    void setSong(int songID);
    void refreshSongLabel();
    void refreshOptions();
    void refreshStyles();
    void setStatus(std::string const& text, cocos2d::ccColor3B color = {255, 255, 255});

    struct Job {
        std::atomic<bool> done{false};
        int stage = 1;          // 1: speed plan, 2: the level
        rg::GenOptions opt;
        rg::Analysis analysis;
        rg::GenResult result;
    };

    LevelEditorLayer* m_editor = nullptr;
    cocos2d::CCLayer* m_main = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCSize m_size;
    cocos2d::CCLabelBMFont* m_songLabel = nullptr;
    cocos2d::CCLabelBMFont* m_seedLabel = nullptr;
    cocos2d::CCLabelBMFont* m_offsetLabel = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::vector<ButtonSprite*> m_diffSprites;
    ButtonSprite* m_stylesSpr = nullptr;
    cocos2d::CCLayer* m_stylePanel = nullptr;
    std::vector<cocos2d::CCSprite*> m_styleChecks;
    std::shared_ptr<Job> m_job;
    geode::Ref<cocos2d::CCNode> m_songLayer = nullptr;
    bool m_busy = false;
    float m_downloadWait = 0;
    std::string m_syncNote;
};
