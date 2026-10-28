#pragma once

#include <Geode/Geode.hpp>

#include "Generator.hpp"

class GeneratorPopup : public geode::Popup, public CustomSongDelegate {
public:
    static GeneratorPopup* create(LevelEditorLayer* editor);

    // CustomSongDelegate (GD's native song menu: Newgrounds, Music Library, NCS)
    void songIDChanged(int id) override;
    int getActiveSongID() override;
    std::string getSongFileName() override;
    LevelSettingsObject* getLevelSettings() override;

protected:
    bool init(LevelEditorLayer* editor);

    void onSetSong(cocos2d::CCObject*);
    void onLibrary(cocos2d::CCObject*);
    void onDifficulty(cocos2d::CCObject* sender);
    void onToggleClear(cocos2d::CCObject*);
    void onToggleEffects(cocos2d::CCObject*);
    void onGenerate(cocos2d::CCObject*);

    void checkDownload(float);
    void startDecode(float);
    void applyResult(rg::GenResult const& res);

    void setSong(int songID);
    void refreshSongLabel();
    void setStatus(std::string const& text, cocos2d::ccColor3B color = {255, 255, 255});

    LevelEditorLayer* m_editor = nullptr;
    geode::TextInput* m_songInput = nullptr;
    geode::TextInput* m_seedInput = nullptr;
    cocos2d::CCLabelBMFont* m_songLabel = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::vector<ButtonSprite*> m_diffSprites;
    int m_difficulty = 2;
    bool m_clear = true;
    bool m_effects = true;
    bool m_busy = false;
    float m_downloadWait = 0;
};
