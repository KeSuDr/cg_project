// main.cpp  (merged: movement + ground + safe texture binding)
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/filesystem.h>
#include <learnopengl/shader_m.h>
#include <learnopengl/animator.h>
#include <learnopengl/model_animation.h>

#include <iostream>
#include <cmath>

#include <vector>
#include <algorithm>
#include <string>
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// ---------- Callbacks ----------
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// ---------- Settings ----------
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
const float PLAYER_JUMP_SPEED = 5.0f;
const float PLAYER_GRAVITY = -9.8f * 2.0f; // stronger gravity for snappier jump

// ---------- Player / Camera ----------
struct Hitbox {
    glm::vec3 center{0.0f, 0.0f, 0.0f};    // world-space center — initialized to avoid C26495
    glm::vec3 halfExtents{0.5f, 0.5f, 0.5f}; // default half extents to avoid C26495
    bool visible = true; // debug draw

    // AABB intersection with another hitbox
    bool intersects(const Hitbox& other) const {
        return std::abs(center.x - other.center.x) <= (halfExtents.x + other.halfExtents.x) &&
            std::abs(center.y - other.center.y) <= (halfExtents.y + other.halfExtents.y) &&
            std::abs(center.z - other.center.z) <= (halfExtents.z + other.halfExtents.z);
    }
};


struct Player {
    glm::vec3 pos{ 0.0f, 0.0f, 0.0f };
    float yawDeg = 0.0f;          // หมุนตัวละครรอบแกน Y
    float moveSpeed = 3.4f;       // m/s (walking)
    float runSpeed = 6.0f;        // m/s (running)
    float rollSpeed = 3.0f;       // m/s
    float height = 1.0f;          // ความสูงศีรษะโดยประมาณ

    // Jump state
    bool isGrounded = true;
    float yVelocity = 0.0f;

} player;


struct Health {
    float maxHP = 100.0f;
    float currentHP = 100.0f;

    void applyDamage(float dmg) {
        currentHP = std::max(0.0f, currentHP - dmg);
    }

    void heal(float amount) {
        currentHP = std::min(maxHP, currentHP + amount);
    }

    bool isDead() const { return currentHP <= 0.0f; }
    float ratio() const { return (maxHP > 0.0f) ? currentHP / maxHP : 0.0f; }
};

struct AttackData {
    std::string name;
    float damage = 10.0f;

    float range = 1.8f;       // ระยะโจมตีโดยรวม
    float jumpDistance = 0.0f;  // ใช้กับ jump attack: ระยะกระโดดรวม
    float duration = 0.8f;    // เวลารวมของท่า
    float hitStart = 0.25f;   // ช่วงเริ่ม active (เป็นสัดส่วน 0-1 ของ duration)
    float hitEnd = 0.55f;   // ช่วงจบ active
    float cooldown = 1.5f;    // เวลาคูลดาวน์
    float windup = 0.6f;        // เวลาก่อนเริ่ม hitbox จริง
    float windupTimer = 0.0f;   // ตัวนับเวลา
    bool windingUp = false;     // กำลังเตรียมโจมตีหรือไม่


    Animation* anim = nullptr; // animation ของท่านี้

    // runtime
    float time = 0.0f;          // เวลาที่ใช้ไปในท่านี้
    float cooldownTimer = 0.0f; // เวลาคูลดาวน์ที่เหลือ
    bool active = false;        // ตอนนี้อยู่ในท่านี้อยู่ไหม
    bool hasHit = false;        // โจมตีโดนแล้วครั้งหนึ่งหรือยัง




    bool animStarted = false;
};

enum class BossState { Idle, Chasing, Attacking, Dead };

struct Boss {
    glm::vec3 pos{ 0.0f, 0.0f, 6.0f };
    float yawDeg = 180.0f;
    float moveSpeed = 2.0f;

    BossState state = BossState::Idle;
    Health hp;

    AttackData attack;          // runtime ของท่าที่กำลังใช้
    AttackData punchTemplate;   // config ท่า punch
    AttackData heavyTemplate;    // config ท่า jump attack
    bool isHeavyAttack = false;  // ตอนนี้กำลังใช้ jump attack อยู่ไหม

    glm::vec3 jumpDirection{ 0.0f, 0.0f, 0.0f }; // ✅ ทิศที่พุ่งตอน jump

    // คูลดาวน์แยกสำหรับ punch / jump
    float punchCdTimer = 0.0f;
    float heavyCdTimer = 0.0f;
    float punchCd = 2.0f;
    float heavyCd = 10.0f;

    Hitbox bodyHitbox;
};



// กล้องแบบ third-person orbit (เมาส์หัน)
struct OrbitCam {
    float yawDeg = 0.0f;
    float pitchDeg = -5.0f;   // เงยขึ้นเล็กน้อย
    float distance = 3.0f;    // เข้าใกล้ตัวละคร
    float height = 0.35f;   // ลดตำแหน่งกล้องลง
    float lookOffset = 0.6f;  // มองเข้าใกล้ระดับอก
    float sens = 0.1f;
    float minPitch = -60.0f;
    float maxPitch = 35.0f;
    float minDist = 1.6f;    // อนุญาตให้ซูมใกล้มากขึ้น
    float maxDist = 6.0f;
} cam;

struct PlayerAttackData {
    float damage = 25.0f;

    float range = 0.9f;
    float duration = 0.5f;  // ความยาวท่า (จะ sync กับ anim speed ของคุณ)
    float hitStart = 0.75f;   // เริ่ม active
    float hitEnd = 0.9f;   // จบ active

    // runtime
    float time = 0.0f;
    bool active = false;
    bool hasHit = false;
};

PlayerAttackData pSlash;           // ✅ ท่าเบา (LMB)
PlayerAttackData pHeavy;           // ✅ ท่าหนัก (RMB)
PlayerAttackData* pCurrentAttack = nullptr; // ใช้ตัวชี้ไปยังท่าที่กำลังใช้


Hitbox playerHitbox;
GLuint hitboxVAO = 0, hitboxVBO = 0, hitboxEBO = 0;
Shader* hitboxShader = nullptr;

// global
Shader* uiShader = nullptr;
GLuint uiVAO = 0, uiVBO = 0;

void InitUI()
{
    uiShader = new Shader("ui.vs", "ui.fs");

    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);

    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    // 2D quad 4 จุด (x,y) — ใช้ glBufferSubData เปลี่ยนทีหลัง
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}


// boss
Boss boss;
Animator* gBossAnimator = nullptr;
const float BOSS_SCALE = 1.8f;

// HP ของ player / boss
Health playerHP;


// hitbox สำหรับท่าโจมตี
Hitbox playerAttackHitbox;
Hitbox bossAttackHitbox;
bool   playerAttackHasHit = false;

bool playerInvulnerable = false;
float iframeTimer = 0.0f;

// ปรับได้ตามต้องการ — ช่วงเวลา i-frame ของท่ากลิ้ง
float ROLL_IFRAME_START = 0.15f;   // วินาทีหลังเริ่มกลิ้งที่เริ่มเป็นอมตะ
float ROLL_IFRAME_END = 0.55f;   // วินาทีที่ i-frame หมด

// ----- Healing potion system -----
int   playerMaxPotions = 5;     // มีได้สูงสุดกี่ขวด
int   playerPotions = 5;     // ตอนเริ่มมี 5 ขวด
float potionHealAmount = 40.0f; // เลือดที่ฟื้นต่อขวด
float potionCooldown = 0.4f;  // กดกินถี่สุดห่างกันเท่าไหร่ (วินาที)
float potionCooldownTimer = 0.0f;  // ตัวนับ cooldown
bool  prevR = false; // สำหรับตรวจ edge กดปุ่ม R

// ---------- Timing ----------
double lastFrame = 0.0;   // keep high precision for time measurement
float deltaTime = 0.0f;

// ---------- Mouse state ----------
bool firstMouse = true;
double lastX = SCR_WIDTH / 2.0;
double lastY = SCR_HEIGHT / 2.0;

// ---------- Input edges ----------
bool prevLMB = false;
bool prevRMB = false;
bool prevSpace = false;
bool prevE = false; // jump key edge
bool prevShift = false; // run key edge
bool prevToggleH = false;


// ---------- Animation State ----------
enum class ActionState { Idle, Moving, Running, Rolling, Attacking, Jumping };
ActionState state = ActionState::Idle;
float actionTimeLeft = 0.0f;

float gPlayerAnimSpeed = 1.0f;

// ---------- GL / Content ----------
Shader* gShader = nullptr;
Model* gModel = nullptr;
Model* gBossModel = nullptr;

Shader* gSwordShader = nullptr;   // << เพิ่มอันนี้
Model* gSwordModel = nullptr;
int gRightHandBoneIndex = -1;
glm::mat4 gRightHandOffset;

Animation* gIdle = nullptr, * gWalk = nullptr, * gRun = nullptr,
* gRoll = nullptr, * gAttack = nullptr, * gJump = nullptr,
* gSlash = nullptr;           // ✅ ท่า Slash ใหม่
Animator* gAnimator = nullptr;

// ----- Boss animations -----
Animation* gBossIdle = nullptr;
Animation* gBossWalk = nullptr;
Animation* gBossPunch = nullptr;
Animation* gBossHeavyAttack = nullptr;  // ✅ ท่า Jump Attack ใหม่



// Ground geometry
GLuint groundVAO = 0, groundVBO = 0, groundEBO = 0;
unsigned int groundTex = 0; // id for ground texture

// ----- helpers -----
static inline float radiansf(float d) { return d * 0.017453292519943295f; }

// ===== Audio (miniaudio) =====
ma_engine gAudioEngine;

// เก็บ pointer ของ ma_sound ที่กำลังเล่นอยู่ เพื่อเคลียร์ทิ้งทีหลัง
std::vector<ma_sound*> gActiveSounds;

// เพลงประกอบ
ma_sound gBgm;
bool gBgmInit = false;



// path ของไฟล์เสียงแต่ละอัน (อิงจาก FileSystem::getPath แบบเดียวกับ model)
std::string gSfxPlayerSlash;
std::string gSfxPlayerHeavy;
std::string gSfxPlayerRoll;
std::string gSfxPlayerJump;
std::string gSfxPlayerPotion;

std::string gSfxBossPunch;
std::string gSfxBossHeavy;

// init / shutdown audio
bool InitAudio()
{
    if (ma_engine_init(NULL, &gAudioEngine) != MA_SUCCESS) {
        std::cout << "Failed to init audio engine\n";
        return false;
    }

    // ลด volume ทั้งเกมลงหน่อย (0.0 - 1.0) ถ้าอยากดังขึ้นค่อยปรับทีหลัง
    ma_engine_set_volume(&gAudioEngine, 0.4f);

    // ---------- BGM (optional) ----------
    std::string bgmPath = FileSystem::getPath("resources/objects/models/bgm.mp3");
    ma_result res = ma_sound_init_from_file(
        &gAudioEngine,
        bgmPath.c_str(),
        MA_SOUND_FLAG_STREAM,    // stream จากไฟล์ (เพลงยาว)
        NULL, NULL,
        &gBgm
    );
    if (res == MA_SUCCESS) {
        gBgmInit = true;
        ma_sound_set_looping(&gBgm, MA_TRUE);
        ma_sound_set_volume(&gBgm, 0.15f);   // ปรับความดังของเพลง
               // ถ้าอยากให้เริ่มตอนเข้าเกม ให้ start ตรงนี้
        // ถ้าอยากเริ่มเฉพาะตอนเข้า GameState::Playing ค่อยไป start ในตอนเปลี่ยน state แทน
    }
    else {
        std::cout << "Failed to load BGM: " << bgmPath << "\n";
    }

    return true;
}

void ShutdownAudio()
{
    // เคลียร์เสียงเอฟเฟกต์ที่ยังค้างอยู่
    for (ma_sound* s : gActiveSounds) {
        ma_sound_uninit(s);
        delete s;
    }
    gActiveSounds.clear();

    if (gBgmInit) {
        ma_sound_uninit(&gBgm);
        gBgmInit = false;
    }

    ma_engine_uninit(&gAudioEngine);
}


// helper เล่นเสียงง่าย ๆ

void PlaySfxDelayed(const std::string& path, float volume, float delaySec)
{
    if (path.empty()) return;

    // สร้าง ma_sound ใหม่บน heap (อย่าบน stack)
    ma_sound* sound = new ma_sound;

    ma_result res = ma_sound_init_from_file(
        &gAudioEngine,
        path.c_str(),
        0,          // flags (ถ้าอยาก stream ค่อยใส่ MA_SOUND_FLAG_STREAM)
        NULL, NULL,
        sound
    );

    if (res != MA_SUCCESS) {
        std::cout << "Failed to init sfx: " << path << "\n";
        delete sound;
        return;
    }

    // ตั้ง volume สำหรับเสียงนี้ (0.0 - 1.0)
    ma_sound_set_volume(sound, volume);

    // แปลงวินาที -> frame โดยอิงเวลาปัจจุบันของ engine
    ma_uint64 nowFrames = ma_engine_get_time_in_pcm_frames(&gAudioEngine);
    ma_uint64 delayFrames = (ma_uint64)(delaySec * ma_engine_get_sample_rate(&gAudioEngine));

    ma_sound_set_start_time_in_pcm_frames(sound, nowFrames + delayFrames);

    // สั่งเริ่มเล่น (แต่จะเริ่มจริงตาม start_time ที่ตั้งไว้)
    ma_sound_start(sound);

    // เก็บ pointer ไว้ให้ main loop มาคอยเช็คว่าเล่นจบแล้วค่อยลบ
    gActiveSounds.push_back(sound);
}


void UpdateSfxLifetime()
{
    for (auto it = gActiveSounds.begin(); it != gActiveSounds.end(); )
    {
        ma_sound* s = *it;

        // ma_sound_at_end() จะ true เมื่อเล่นจบ (รวมถึงกรณีไม่ start ก็จะไม่เป็น true)
        if (ma_sound_at_end(s)) {
            ma_sound_uninit(s);
            delete s;
            it = gActiveSounds.erase(it);
        }
        else {
            ++it;
        }
    }
}





// ----- Player helpers -----
void PlayLoop(Animation* anim, float speed = 1.0f) {
    gPlayerAnimSpeed = speed;
    gAnimator->PlayAnimation(anim);
}

void PlayOneShot(Animation* anim, float& outSec, float speed = 1.0f) {
    gPlayerAnimSpeed = speed;
    gAnimator->PlayAnimation(anim);

    float durTicks = anim->GetDuration();
    float tps = anim->GetTicksPerSecond();
    float baseSec = (tps > 0.0f) ? durTicks / tps : 0.7f;

    // เล่นเร็วขึ้น -> ระยะเวลาท่าควรสั้นลงตาม
    outSec = baseSec / speed;
}


// ----- Boss helpers -----
void BossPlayLoop(Animation* anim) {
    if (gBossAnimator)
        gBossAnimator->PlayAnimation(anim);
}

void BossPlayOneShot(Animation* anim) {
    if (gBossAnimator) {
        gBossAnimator->PlayAnimation(anim);
        // ไม่ต้องคำนวณเวลาเพิ่ม ใช้ boss.attack.duration เอา
    }
}

void DrawRect(float x, float y, float w, float h, glm::vec3 color)
{
    float verts[8] = {
        x,     y,
        x + w,   y,
        x + w,   y + h,
        x,     y + h
    };

    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    uiShader->setVec3("color", color);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glBindVertexArray(0);
}
void DrawTextUI(const char* text, float x, float y, float size, glm::vec3 color);
// Helper: draw text centered inside a rectangle (boxX,boxY = bottom-left)
void DrawTextCenteredInBox(const char* text, float boxX, float boxY, float boxW, float boxH, float size, glm::vec3 color) {
    // simple strlen (avoid adding headers)
    int len = 0; while (text[len]) ++len;
    float spacing = size * 0.8f;            // same spacing used by DrawTextUI
    float textWidth = (len > 0) ? spacing * len - (spacing - size * 0.6f) : 0.0f; // approximate actual glyph width
    float textHeight = size;
    float tx = boxX + (boxW - textWidth) * 0.5f;
    float ty = boxY + (boxH - textHeight) * 0.5f;
    DrawTextUI(text, tx, ty, size, color);
}


// เวกเตอร๋ forward/right “ตามกล้อง” (ใช้กับ WASD)
glm::vec3 CameraForward() {
    float yaw = radiansf(cam.yawDeg);
    float pit = radiansf(cam.pitchDeg);
    glm::vec3 f;
    f.x = std::cos(pit) * std::sin(yaw);
    f.y = std::sin(pit);
    f.z = std::cos(pit) * std::cos(yaw);
    // ใช้เฉพาะบนระนาบ XZ สำหรับทิศเดิน
    f.y = 0.0f;
    if (glm::length(f) < 1e-6f) f = glm::vec3(0, 0, 1);
    return glm::normalize(f);
}
glm::vec3 CameraRight() {
    glm::vec3 f = CameraForward();
    return glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
}

// กล้อง: คำนวณตำแหน่งและ view
void ComputeCamera(glm::vec3& outPos, glm::mat4& outView) {
    // ทิศทางกล้องเต็ม (รวม pitch)
    float yaw = radiansf(cam.yawDeg);
    float pit = radiansf(cam.pitchDeg);
    glm::vec3 dir;
    dir.x = std::cos(pit) * std::sin(yaw);
    dir.y = std::sin(pit);
    dir.z = std::cos(pit) * std::cos(yaw);

    glm::vec3 target = player.pos + glm::vec3(0, player.height + cam.lookOffset, 0);
    outPos = target - dir * cam.distance + glm::vec3(0, cam.height, 0);
    outView = glm::lookAt(outPos, target, glm::vec3(0, 1, 0));
}

// สร้างพื้นเป็นสี่เหลี่ยมใหญ่ (ตำแหน่ง, นอร์มัล, เท็กซ์โค)
void CreateGround() {
    const float S = 100.0f; // ครึ่งหนึ่ง (รวมคือ 200x200)
    // pos(x,y,z) normal(x,y,z) tex(u,v)
    float verts[] = {
        -S, 0.0f, -S,   0,1,0,   0.0f, 0.0f,
         S, 0.0f, -S,   0,1,0,  50.0f, 0.0f,
         S, 0.0f,  S,   0,1,0,  50.0f,50.0f,
        -S, 0.0f,  S,   0,1,0,   0.0f,50.0f
    };
    unsigned int idx[] = { 0,1,2,  0,2,3 };

    glGenVertexArrays(1, &groundVAO);
    glGenBuffers(1, &groundVBO);
    glGenBuffers(1, &groundEBO);

    glBindVertexArray(groundVAO);
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    // สมมติ anim_model.vs ใช้ layout:
    // location 0: position, 1: normal, 2: texcoord
    GLsizei stride = (3 + 3 + 2) * sizeof(float);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
}

// load a 2D texture from path and return GL id (0 on fail)
unsigned int LoadTexture(const std::string& path) {
    int width, height, nrChannels;
    // stbi flip already set in main
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 0);
    if (!data) {
        std::cout << "Failed to load texture: " << path << "\n";
        return 0;
    }

    GLenum format = GL_RGB;
    if (nrChannels == 1) format = GL_RED;
    else if (nrChannels == 3) format = GL_RGB;
    else if (nrChannels == 4) format = GL_RGBA;

    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void SetIdentityBones(Shader* shader, size_t count)
{
    glm::mat4 I(1.0f);
    for (size_t i = 0; i < count; ++i) {
        shader->setMat4("finalBonesMatrices[" + std::to_string(i) + "]", I);
    }
}


void CreateHitboxMesh() {
    float verts[] = {
        // 8 corner points of unit cube centered at origin
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,0.5f,-0.5f,  -0.5f,0.5f,-0.5f,
        -0.5f,-0.5f,0.5f,   0.5f,-0.5f,0.5f,   0.5f,0.5f,0.5f,   -0.5f,0.5f,0.5f
    };
    unsigned int idx[] = {
        0,1, 1,2, 2,3, 3,0, // bottom
        4,5, 5,6, 6,7, 7,4, // top
        0,4, 1,5, 2,6, 3,7  // sides
    };

    glGenVertexArrays(1, &hitboxVAO);
    glGenBuffers(1, &hitboxVBO);
    glGenBuffers(1, &hitboxEBO);

    glBindVertexArray(hitboxVAO);

    glBindBuffer(GL_ARRAY_BUFFER, hitboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hitboxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}


void DrawHitbox(Hitbox& hb) {
    if (!hb.visible) return;

    hitboxShader->use();

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, hb.center);
    model = glm::scale(model, hb.halfExtents * 2.0f);

    glm::mat4 projection = glm::perspective(glm::radians(50.0f),
        (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 300.0f);
    glm::vec3 camPos; glm::mat4 view;
    ComputeCamera(camPos, view);

    hitboxShader->setMat4("projection", projection);
    hitboxShader->setMat4("view", view);
    hitboxShader->setMat4("model", model);

    glBindVertexArray(hitboxVAO);
    glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// --- Game state enum and globals (safe additions) ---
enum class GameState { Menu, Playing, GameOver, Victory };
GameState gGameState = GameState::Menu;
bool gPrevEnter = false;
bool gPrintedMenuMsg = false;
bool gPrintedGameOverMsg = false;
bool gPrintedVictoryMsg = false;

// Reset helper: restores gameplay state without touching GL resources like groundVAO
void ResetGame()
{
    // player
    player.pos = glm::vec3(0.0f, 0.0f, 0.0f);
    player.yawDeg = 0.0f;
    player.isGrounded = true;
    player.yVelocity = 0.0f;

    // HP
    playerHP.maxHP = 100.0f;
    playerHP.currentHP = 100.0f;
    boss.hp.maxHP = 1000.0f;
    boss.hp.currentHP = 1000.0f;

    // boss
    boss.pos = player.pos + glm::vec3(0.0f, 0.0f, 12.0f);
    boss.yawDeg = 180.0f;
    boss.moveSpeed = 2.0f;
    boss.state = BossState::Idle;
    boss.bodyHitbox.visible = true;

    // reset boss attack runtime and cooldowns so it won't immediately re-hit player
    boss.attack = boss.punchTemplate;
    boss.attack.active = false;
    boss.attack.time = 0.0f;
    boss.attack.hasHit = false;
    boss.attack.windingUp = false;
    boss.attack.windupTimer = 0.0f;
    boss.attack.animStarted = false;
    boss.attack.cooldownTimer = 0.0f;
    boss.punchCdTimer = 1.0f;   // small grace period after reset
    boss.heavyCdTimer = 1.0f;
    bossAttackHitbox.visible = false;

    // attacks & hitboxes
    playerAttackHitbox.visible = false;
    pCurrentAttack = nullptr;
    playerAttackHasHit = false;

    // animation & state
    state = ActionState::Idle;
    actionTimeLeft = 0.0f;
    gPlayerAnimSpeed = 1.0f;
    if (gAnimator && gIdle) PlayLoop(gIdle);
    if (gBossAnimator && gBossIdle) BossPlayLoop(gBossIdle);

    // timers / flags
    playerInvulnerable = false;
    iframeTimer = 0.0f;
    potionCooldownTimer = 0.0f;
    playerPotions = playerMaxPotions;

    // console flags
    gPrintedMenuMsg = false;
    gPrintedGameOverMsg = false;
    gPrintedVictoryMsg = false;
}

int main() {
    // ---- GLFW/GL setup ----
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Dark Arena", NULL, NULL);
    if (!window) { std::cout << "Failed to create GLFW window\n"; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    // จับเมาส์ (เหมือนเกม)
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    // Flip once globally for stb (match your model textures / UVs)
    stbi_set_flip_vertically_on_load(true);
    glEnable(GL_DEPTH_TEST);

    InitAudio();

    // ตั้ง path ไฟล์เสียง (อยู่ในโฟลเดอร์เดียวกับโมเดล)
    gSfxPlayerSlash = FileSystem::getPath("resources/objects/models/punch.mp3");
    gSfxPlayerHeavy = FileSystem::getPath("resources/objects/models/heavypunch.mp3");
    gSfxPlayerRoll = FileSystem::getPath("resources/objects/models/roll.mp3");
    gSfxPlayerJump = FileSystem::getPath("resources/objects/models/roll.mp3");
    gSfxPlayerPotion = FileSystem::getPath("resources/objects/models/drink.mp3");

    gSfxBossPunch = FileSystem::getPath("resources/objects/models/heavypunch.mp3");
    gSfxBossHeavy = FileSystem::getPath("resources/objects/models/heavy.mp3");

    // ---- Shaders ----
    Shader ourShader("anim_model.vs", "anim_model.fs");
    gShader = &ourShader;

    Shader swordShader("sword.vs", "sword.fs");
    gSwordShader = &swordShader;

    // หลังจาก gShader โหลดเสร็จ
    hitboxShader = new Shader(
        "hitbox.vs", // vertex shader
        "hitbox.fs"  // fragment shader
    );

    InitUI();


    // ---- Load Model & Animations ----
    Model  ourModel(FileSystem::getPath("resources/objects/models/idle.dae"));
    gModel = &ourModel;

    Model  bossModel(FileSystem::getPath("resources/objects/models/boss_idle.dae"));
    gBossModel = &bossModel;

    Model swordModel(FileSystem::getPath("resources/objects/models/Sword_2.fbx"));
    gSwordModel = &swordModel;

    // DEBUG: check path + file exists
    std::string swordPath = FileSystem::getPath("resources/objects/models/Sword_2.fbx");
    std::cout << "Sword path: " << swordPath << "\n";
    std::cout << "Sword file exists? "
        << std::boolalpha << std::filesystem::exists(swordPath) << "\n";

    // DEBUG: check mesh count (ใน LearnOpenGL Model, meshes เป็น public)
    std::cout << "Sword mesh count = " << gSwordModel->meshes.size() << "\n";


    Animation idleAnim(FileSystem::getPath("resources/objects/models/idle.dae"), &ourModel);
    Animation walkAnim(FileSystem::getPath("resources/objects/models/walk.dae"), &ourModel);
    Animation walkBackwardAnim(FileSystem::getPath("resources/objects/models/walk_backward.dae"), &ourModel);
    Animation runAnim(FileSystem::getPath("resources/objects/models/run.dae"), &ourModel);
    Animation strafeLeftAnim(FileSystem::getPath("resources/objects/models/strafe_left.dae"), &ourModel);
    Animation strafeRightAnim(FileSystem::getPath("resources/objects/models/strafe_right.dae"), &ourModel);
    Animation rollAnim(FileSystem::getPath("resources/objects/models/roll.dae"), &ourModel);
    Animation attackAnim(FileSystem::getPath("resources/objects/models/attack.dae"), &ourModel);
    Animation jumpAnim(FileSystem::getPath("resources/objects/models/jump.dae"), &ourModel);
    Animation slashAnim(
        FileSystem::getPath("resources/objects/models/slash.dae"), &ourModel
    );

    gIdle = &idleAnim;
    gWalk = &walkAnim;
    gRun = &runAnim;
    gRoll = &rollAnim;
    gAttack = &attackAnim;
    gJump = &jumpAnim;
    gSlash = &slashAnim;

    Animator animator(gIdle);
    gAnimator = &animator;

    // ---- Boss Animations ----
    Animation bossIdleAnim(
        FileSystem::getPath("resources/objects/models/boss_idle.dae"),
        gBossModel
    );
    Animation bossWalkAnim(
        FileSystem::getPath("resources/objects/models/boss_walk.dae"),
        gBossModel
    );
    Animation bossPunchAnim(
        FileSystem::getPath("resources/objects/models/boss_punch.dae"),
        gBossModel
    );

    Animation bossHeavyAttackAnim(
        FileSystem::getPath("resources/objects/models/boss_heavy_attack.dae"),
        gBossModel
    );

    gBossIdle = &bossIdleAnim;
    gBossWalk = &bossWalkAnim;
    gBossPunch = &bossPunchAnim;
    gBossHeavyAttack = &bossHeavyAttackAnim;

	//for checking file path issues
    /*std::cout << "CWD: " << std::filesystem::current_path() << "\n";
    std::cout << "`ui.vs` exists? " << std::boolalpha << std::filesystem::exists("ui.vs") << "\n";*/

    // สมมติในโมเดลใช้ชื่อ bone แบบ mixamo
    std::string rightHandName = "mixamorig_RightHand";

    auto& boneInfoMap = gModel->GetBoneInfoMap();
    auto it = boneInfoMap.find(rightHandName);
    if (it != boneInfoMap.end()) {
        gRightHandBoneIndex = it->second.id;
        std::cout << "Right hand bone index: " << gRightHandBoneIndex << "\n";
    }
    else {
        std::cout << "Right hand bone not found!\n";
    }



    // default: boss idle animation
    Animator bossAnimator(gBossIdle);
    gBossAnimator = &bossAnimator;


    // ตั้ง HP
    playerHP.maxHP = playerHP.currentHP = 100.0f;
    boss.hp.maxHP = boss.hp.currentHP = 1000.0f;

    // spawn boss ข้างหน้าผู้เล่นนิดหน่อย
    boss.pos = player.pos + glm::vec3(0.0f, 0.0f, 12.0f); // เดิม 6.0f -> 12.0f
    boss.yawDeg = 180.0f;
    boss.moveSpeed = 2.0f;
    boss.state = BossState::Idle;
    BossPlayLoop(gBossIdle);  // ให้เล่น idle ชัด ๆ เลย

    // hitbox ตัวบอส
    boss.bodyHitbox.halfExtents = glm::vec3(0.5f, 1.0f, 0.4f) * BOSS_SCALE;  // 👈 คูณ scale
    boss.bodyHitbox.visible = true;


    // ตั้งค่าท่าโจมตีหลักของบอส
// ตั้งค่าท่าโจมตีหลักของบอส
// ----- Punch template -----
// damage, range, duration, windup, cooldown ปรับตามต้องการ
    boss.punchTemplate.name = "Punch";
    boss.punchTemplate.damage = 20.0f;

    boss.punchTemplate.range = 2.0f;
    boss.punchTemplate.jumpDistance = 0.0f;
    boss.punchTemplate.windup = 0.7f;
    boss.punchTemplate.duration = 0.8f;

    boss.punchTemplate.hitStart = 0.3f;
    boss.punchTemplate.hitEnd = 0.6f;

    boss.punchTemplate.cooldown = 2.5f;
    boss.punchTemplate.anim = gBossPunch;

    // ตั้ง attack runtime เริ่มจาก punch ไปก่อน (ไม่สำคัญมาก เดี๋ยวตอนเริ่มท่าจะ override)
    boss.attack = boss.punchTemplate;
    boss.attack.time = 0.0f;
    boss.attack.cooldownTimer = 0.0f;
    boss.attack.active = false;
    boss.attack.hasHit = false;
    boss.attack.windingUp = false;
    boss.attack.windupTimer = 0.0f;
    boss.attack.animStarted = false;
    boss.isHeavyAttack = false;

    // ----- Heavy Attack template (ทุบลงพื้น AOE) -----
// แรงกว่า damage, ระยะ AoE, พักทำซ้ำ คูลดาวน์ นานกว่า
    boss.heavyTemplate.name = "HeavyAttack";
    boss.heavyTemplate.damage = 60.0f;    // ท่าหนัก แรงกว่าหมัด

    // ระยะที่ถือว่าอยู่ในระยะใช้ท่านี้ (ใช้กับ AI เท่านั้น)
    boss.heavyTemplate.range = boss.punchTemplate.range;

    // ไม่ต้อง “กระโดด” จริงแล้ว แต่ให้มีจังหวะง้างก่อนทุบ
    boss.heavyTemplate.windup = 0.6f;

    // ความยาว animation ช่วงโจมตี (ตามไฟล์ boss_jump_attack.dae)
    boss.heavyTemplate.duration = 2.0f;   // ถ้าอนิเมชันยาวกว่านี้ค่อยไปปรับทีหลัง

    // ให้ hitbox active เฉพาะท้าย ๆ ของท่า
    boss.heavyTemplate.hitStart = 0.75f;
    boss.heavyTemplate.hitEnd = 0.95f;

    boss.heavyTemplate.cooldown = 5.0f;   // คูลดาวน์นานกว่าหมัด
    boss.heavyTemplate.anim = gBossHeavyAttack;  // หรือ gBossJumpAttack ถ้าไม่ได้ rename



    // ---- Player attack settings ----
    // ท่า Slash (LMB) เบากว่า เร็ว
    pSlash.damage = 15.0f;
    pSlash.range = 0.9f;
    pSlash.duration = 0.45f;
    pSlash.hitStart = 0.35f;
    pSlash.hitEnd = 0.65f;

    // ท่า Heavy (RMB) = ของเดิมที่เคยใช้ แต่ตั้งใหม่ให้ชัด
    pHeavy.damage = 25.0f;
    pHeavy.range = 1.0f;
    pHeavy.duration = 0.6f;
    pHeavy.hitStart = 0.95f;
    pHeavy.hitEnd = 1.0f;

    // player attack hitbox default
    playerAttackHitbox.visible = false;
    bossAttackHitbox.visible = false;



    // ---- Ground ----
    CreateGround();

    // ---- Load ground texture (change path if needed) ----
    std::string groundTexPath = FileSystem::getPath("resources/objects/models/textures/ground.png");
    groundTex = LoadTexture(groundTexPath);
    if (groundTex == 0) {
        std::cout << "Warning: ground texture not loaded, ground will still draw with shader default.\n";
    }

    // toggle hitbox ก่อน main loop
    CreateHitboxMesh();
    bool showHitbox = false;

    // -------- Main loop --------
    while (!glfwWindowShouldClose(window)) {
        // --- timing ---
        double currentFrame = glfwGetTime();
        deltaTime = static_cast<float>(currentFrame - lastFrame);
        lastFrame = currentFrame;

        // --- input ---
        glm::vec2 moveInput(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveInput.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveInput.y -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveInput.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveInput.x -= 1.0f;

        bool spaceNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool lmbNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rmbNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        bool shiftNow = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
        bool rNow = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;   // ✅ กินยา

        // --- potion cooldown update ---
        if (potionCooldownTimer > 0.0f) {
            potionCooldownTimer -= deltaTime;
            if (potionCooldownTimer < 0.0f) potionCooldownTimer = 0.0f;
        }

        // --- ใช้ยาเมื่อกด R (edge) ---
        if (rNow && !prevR && !playerHP.isDead()) {
            if (playerPotions > 0 && potionCooldownTimer <= 0.0f && playerHP.currentHP < playerHP.maxHP) {
                playerHP.heal(potionHealAmount);
                playerPotions--;
                potionCooldownTimer = potionCooldown;

                std::cout << "[Potion] Heal +" << potionHealAmount
                    << " HP, now = " << playerHP.currentHP
                    << ", potions left = " << playerPotions << "\n";

                PlaySfxDelayed(gSfxPlayerPotion, 0.5f, 0.0f);
            }
        }


        // toggle hitbox with H (edge detect)
        bool hNow = glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS;
        if (hNow && !prevToggleH) {
            showHitbox = !showHitbox;
        }
        prevToggleH = hNow;


        // --- update player state, gravity, movement ---
        // ===== STATE MACHINE =====
        if (state == ActionState::Rolling || state == ActionState::Attacking) {
            actionTimeLeft -= deltaTime;
            if (actionTimeLeft <= 0.0f) {

                // กลิ้งจบ → ปิด I-frame
                playerInvulnerable = false;

                if (glm::length(moveInput) > 0.0f) {
                    if (shiftNow) { state = ActionState::Running; PlayLoop(gRun); }
                    else { state = ActionState::Moving; PlayLoop(gWalk); }
                }
                else {
                    state = ActionState::Idle;   PlayLoop(gIdle);
                }
            }
        }

        else if (state == ActionState::Jumping)
        {
            // landing handled by gravity block below; keep playing jump until landed
            if (player.isGrounded)
            {
                // landed this frame
                if (glm::length(moveInput) > 0.0f) {
                    if (shiftNow) { state = ActionState::Running; PlayLoop(gRun); }
                    else { state = ActionState::Moving; PlayLoop(gWalk); }
                }
                else { state = ActionState::Idle; PlayLoop(gIdle); }
            }
        }
        else {
            if (eNow && !prevE && player.isGrounded) {
                // start jump
                state = ActionState::Jumping;
                PlayOneShot(gJump, actionTimeLeft);
                player.yVelocity = PLAYER_JUMP_SPEED;
                player.isGrounded = false;

                PlaySfxDelayed(gSfxPlayerJump, 0.5f, 0.0f);
            }
            else if (spaceNow && !prevSpace) {
                state = ActionState::Rolling;
                PlayOneShot(gRoll, actionTimeLeft, 2.0f);

                iframeTimer = 0.0f;
                playerInvulnerable = false;   // จะเปิดหลังถึงเวลา ROLL_IFRAME_START

                PlaySfxDelayed(gSfxPlayerRoll, 0.5f, 0.0f);
            }

            else if (lmbNow && !prevLMB) {
                // LMB = ท่า Slash (light)
                state = ActionState::Attacking;
                PlayOneShot(gSlash, actionTimeLeft, 2.5f);  // เล่น anim slash.dae เร็วหน่อยก็ได้

                pCurrentAttack = &pSlash;
                pCurrentAttack->active = true;
                pCurrentAttack->time = 0.0f;
                pCurrentAttack->hasHit = false;

                PlaySfxDelayed(gSfxPlayerSlash, 0.5f, 0.0f);
            }

            else if (rmbNow && !prevRMB) {
                // RMB = ท่าหนักเดิม (attack.dae)
                state = ActionState::Attacking;
                PlayOneShot(gAttack, actionTimeLeft, 3.0f);  // heavy ช้ากว่าเล็กน้อย

                pCurrentAttack = &pHeavy;
                pCurrentAttack->active = true;
                pCurrentAttack->time = 0.0f;
                pCurrentAttack->hasHit = false;

                PlaySfxDelayed(gSfxPlayerHeavy, 0.5f, 0.6f);
            }

            else {
                // If moving and holding shift -> running
                if (glm::length(moveInput) > 0.0f) {
                    if (shiftNow) {
                        if (state != ActionState::Running) { state = ActionState::Running; PlayLoop(gRun, 1.0f); }
                    }
                    else {
                        if (state != ActionState::Moving) { state = ActionState::Moving; PlayLoop(gWalk, 1.0f); }
                    }
                }
                else {
                    if (state != ActionState::Idle) { state = ActionState::Idle;   PlayLoop(gIdle, 1.0f); }
                }
            }
            prevLMB = lmbNow;
        }

        // ===== GRAVITY / JUMP =====
        if (!player.isGrounded)
        {
            player.yVelocity += PLAYER_GRAVITY * deltaTime;
            player.pos.y += player.yVelocity * deltaTime;

            if (player.pos.y <= 0.0f)
            {
                player.pos.y = 0.0f;
                player.yVelocity = 0.0f;
                player.isGrounded = true;
                // state change handled at top of loop (or force here)
                if (state == ActionState::Jumping) {
                    if (glm::length(moveInput) > 0.0f) {
                        if (shiftNow) { state = ActionState::Running; PlayLoop(gRun); }
                        else { state = ActionState::Moving; PlayLoop(gWalk); }
                    }
                    else { state = ActionState::Idle; PlayLoop(gIdle); }
                }
            }
        }

        // ===== MOVEMENT =====
        glm::vec3 camF = CameraForward();
        glm::vec3 camR = CameraRight();
        glm::vec3 wishDir = glm::vec3(0.0f);
        if (glm::length(moveInput) > 1e-6f) {
            wishDir = glm::normalize(camF * moveInput.y + camR * moveInput.x);
            if (glm::any(glm::isnan(wishDir))) wishDir = glm::vec3(0);
        }

        if (state == ActionState::Moving || state == ActionState::Idle || state == ActionState::Jumping || state == ActionState::Running) {
            float spd = 0.0f;
            if (state == ActionState::Moving) spd = player.moveSpeed;
            else if (state == ActionState::Running) spd = player.runSpeed;
            else spd = 0.0f;
            // allow limited air-control while jumping
            if (state == ActionState::Jumping) spd *= 0.6f;
            player.pos += wishDir * spd * deltaTime;

            if (glm::length(wishDir) > 0.0f) {
                // หันตัวละครไปทิศการเคลื่อนที่ (souls-like)
                player.yawDeg = glm::degrees(std::atan2(wishDir.x, wishDir.z));
            }
        }
        else if (state == ActionState::Rolling) {
            // อัปเดต i-frame timer
            iframeTimer += deltaTime;

            // เปิด i-frame
            if (iframeTimer >= ROLL_IFRAME_START && iframeTimer <= ROLL_IFRAME_END) {
                playerInvulnerable = true;
            }
            else {
                playerInvulnerable = false;
            }

            // กลิ้งพุ่งไปข้างหน้า (ตามเดิม)
            glm::vec3 forwardChar = glm::normalize(glm::vec3(
                std::sin(radiansf(player.yawDeg)), 0,
                std::cos(radiansf(player.yawDeg))
            ));
            player.pos += forwardChar * player.rollSpeed * deltaTime;
        }


        prevSpace = spaceNow;
        prevLMB = lmbNow;
        prevRMB = rmbNow;
        prevE = eNow;
        prevShift = shiftNow;
		prevR = rNow;

        if (pCurrentAttack && pCurrentAttack->active) {
            pCurrentAttack->time += deltaTime;

            float hitStartTime = pCurrentAttack->hitStart * pCurrentAttack->duration;
            float hitEndTime = pCurrentAttack->hitEnd * pCurrentAttack->duration;

            playerAttackHitbox.visible = false;

            if (pCurrentAttack->time >= hitStartTime &&
                pCurrentAttack->time <= hitEndTime) {

                glm::vec3 fwd = glm::normalize(glm::vec3(
                    std::sin(radiansf(player.yawDeg)), 0,
                    std::cos(radiansf(player.yawDeg))
                ));

                playerAttackHitbox.center =
                    player.pos + glm::vec3(0, player.height * 1.0f, 0)
                    + fwd * pCurrentAttack->range;

                playerAttackHitbox.halfExtents = glm::vec3(0.5f, 1.0f, 0.6f);
                playerAttackHitbox.visible = true;

                if (!pCurrentAttack->hasHit &&
                    playerAttackHitbox.intersects(boss.bodyHitbox)) {

                    boss.hp.applyDamage(pCurrentAttack->damage);
                    pCurrentAttack->hasHit = true;

                    std::cout << "[Player] hit boss for "
                        << pCurrentAttack->damage
                        << " dmg. Boss HP = " << boss.hp.currentHP << "\n";

                    // If boss died, enter Victory UI
                    if (boss.hp.isDead() && gGameState != GameState::Victory) {
                        gGameState = GameState::Victory;
                        gPrintedVictoryMsg = false; // allow console message on entry
                        // stop boss attacks/visibility
                        bossAttackHitbox.visible = false;
                        boss.state = BossState::Dead;
                        std::cout << "Boss defeated! Switching to VICTORY state.\n";
                    }
                }
            }

            if (pCurrentAttack->time >= pCurrentAttack->duration) {
                pCurrentAttack->active = false;
                playerAttackHitbox.visible = false;
                pCurrentAttack = nullptr;
            }
        }



        // --- player body hitbox (แก้ตามข้อ 4) ---
        playerHitbox.center = player.pos + glm::vec3(0, player.height / 1.25f, 0);
        playerHitbox.halfExtents = glm::vec3(0.3f, player.height, 0.3f);
        playerHitbox.visible = true;


        // --- boss body hitbox ---
        boss.bodyHitbox.center = boss.pos + glm::vec3(0, boss.bodyHitbox.halfExtents.y, 0);

        // --- boss AI ---
        const float bossAggroRadius = 10.0f;
        const float punchRange = boss.punchTemplate.range;
        const float heavyRange = boss.heavyTemplate.range;

        // ลด cooldown รวม (ใช้ cooldown ตัวเดียวง่าย ๆ)
        if (boss.attack.cooldownTimer > 0.0f) {
            boss.attack.cooldownTimer -= deltaTime;
        }

        boss.punchCdTimer -= deltaTime;
        if (boss.punchCdTimer < 0.0f) boss.punchCdTimer = 0.0f;

        boss.heavyCdTimer -= deltaTime;
        if (boss.heavyCdTimer < 0.0f) boss.heavyCdTimer = 0.0f;




        if (!boss.hp.isDead()) {
            glm::vec3 toPlayer3 = player.pos - boss.pos;
            glm::vec2 toPlayerXZ(toPlayer3.x, toPlayer3.z);
            float dist = glm::length(toPlayerXZ);

            switch (boss.state) {
            case BossState::Idle: {
                // เห็นผู้เล่นเมื่อเข้า radius
                if (dist < bossAggroRadius) {
                    boss.state = BossState::Chasing;
                    BossPlayLoop(gBossWalk);
                }
                break;
            }
            case BossState::Chasing: {
                if (dist > bossAggroRadius * 1.3f) {
                    // ผู้เล่นหนีไกลเกิน กลับไป idle
                    boss.state = BossState::Idle;
                    BossPlayLoop(gBossIdle);
                }
                else {
                    // หันหน้าเข้าหาผู้เล่น
                    if (dist > 0.1f) {
                        boss.yawDeg = glm::degrees(std::atan2(toPlayer3.x, toPlayer3.z));
                    }

                    // ===== 1) เดินเข้าไปหา player =====
                    float maxAttackRange = std::max(punchRange, heavyRange);
                    float desiredDist = maxAttackRange * 0.8f;  // ระยะที่อยากหยุดเพื่อเริ่มโจมตี

                    if (dist > desiredDist) {
                        // ยังไกลอยู่ → ขยับตำแหน่งเข้าไป
                        glm::vec3 dir = glm::normalize(glm::vec3(toPlayer3.x, 0.0f, toPlayer3.z));
                        boss.pos += dir * boss.moveSpeed * deltaTime;
                    }
                    else {
                        // ===== 2) อยู่ในระยะโจมตีแล้ว → เลือกท่า =====
                        bool inRange = (dist <= punchRange);   // หรือ <= heavyRange ก็เท่ากันอยู่แล้ว

                        bool canPunch = (inRange && boss.punchCdTimer <= 0.0f);
                        bool canHeavy = (inRange && boss.heavyCdTimer <= 0.0f);

                        if (!canPunch && !canHeavy) {
                            // ทั้งสองท่าใช้ไม่ได้ (คูลดาวน์ไม่ครบ) → เดินไล่เฉย ๆ
                        }
                        else {
                            bool useHeavy = false;

                            if (canHeavy && !canPunch)      useHeavy = true;
                            else if (canPunch && !canHeavy) useHeavy = false;
                            else {
                                // ทั้งสองท่าใช้ได้ → random เอา
                                int r = rand() % 100;
                                useHeavy = (r < 40);   // 40% ใช้ heavy, 60% ใช้ punch
                            }

                            boss.isHeavyAttack = useHeavy;
                            const AttackData& tpl = useHeavy ? boss.heavyTemplate : boss.punchTemplate;
                            boss.attack = tpl;

                            boss.attack.active = true;
                            boss.attack.time = 0.0f;
                            boss.attack.hasHit = false;
                            boss.attack.windingUp = true;
                            boss.attack.windupTimer = boss.attack.windup;
                            boss.attack.animStarted = false;

                            if (useHeavy) {
                                boss.heavyCdTimer = boss.heavyCd;  // ยังนานกว่า punch
                            }
                            else {
                                boss.punchCdTimer = boss.punchCd;
                            }

                            boss.state = BossState::Attacking;
                        }
                    }
                }
                break;
            }

            case BossState::Attacking: {
                // ---------- ช่วง windup: รอก่อนชก ----------
                if (boss.attack.windingUp) {
                    boss.attack.windupTimer -= deltaTime;
                    bossAttackHitbox.visible = false;

                    if (boss.attack.windupTimer <= 0.0f) {
                        boss.attack.windingUp = false;
                        boss.attack.time = 0.0f;
                        boss.attack.animStarted = false;

                    }
                    break;
                }

                // ---------- เริ่มเล่นอนิเมชั่นโจมตีจริง ----------
                if (!boss.attack.animStarted) {
                    BossPlayOneShot(boss.attack.anim);
                    boss.attack.animStarted = true;

                    if (boss.isHeavyAttack) {
                        PlaySfxDelayed(gSfxBossHeavy, 0.5f, 1.0f);
                    }
                    else {
                        PlaySfxDelayed(gSfxBossPunch, 0.5f, 0.0f);
                    }
                }

                boss.attack.time += deltaTime;
                bossAttackHitbox.visible = false;

                float t = boss.attack.time;
                float hitStartTime = boss.attack.hitStart * boss.attack.duration;
                float hitEndTime = boss.attack.hitEnd * boss.attack.duration;

                if (t >= hitStartTime && t <= hitEndTime) {
                    if (!boss.isHeavyAttack) {
                        // ----- ท่า Punch ปกติ -----
                        glm::vec3 fwd = glm::normalize(glm::vec3(
                            std::sin(radiansf(boss.yawDeg)), 0,
                            std::cos(radiansf(boss.yawDeg))
                        ));

                        bossAttackHitbox.center = boss.pos
                            + glm::vec3(0, boss.bodyHitbox.halfExtents.y, 0)
                            + fwd * (boss.attack.range * 0.6f);

                        bossAttackHitbox.halfExtents = glm::vec3(0.7f, 0.7f, boss.attack.range * 0.6f);
                    }
                    else {
                        // ----- Heavy Attack: AOE ทุบลงพื้นรอบตัว -----
                        float radius = 4.5f;   // ขนาดวงทุบ
                        bossAttackHitbox.center = boss.pos + glm::vec3(0, boss.bodyHitbox.halfExtents.y, 0);
                        bossAttackHitbox.halfExtents = glm::vec3(radius);
                    }

                    bossAttackHitbox.visible = true;

                    if (!boss.attack.hasHit && bossAttackHitbox.intersects(playerHitbox)) {
                        if (!playerInvulnerable) {
                            playerHP.applyDamage(boss.attack.damage);
                            std::cout << "[Boss] "
                                << (boss.isHeavyAttack ? "HeavyAttack" : "Punch")
                                << " hit player! Player HP = " << playerHP.currentHP << "\n";

                            // Transition to GameOver when player dies
                            if (playerHP.isDead() && gGameState != GameState::GameOver) {
                                gGameState = GameState::GameOver;
                                gPrintedGameOverMsg = false; // allow the game-over console message to print
                                std::cout << "Player died. Switching to GAME OVER state.\n";
                            }
                        }
                        else {
                            std::cout << "[I-FRAME] Player dodged boss "
                                << (boss.isHeavyAttack ? "HeavyAttack" : "Punch") << "\n";
                        }
                        boss.attack.hasHit = true;
                    }
                }



                // ----- จบท่าแล้ว -----
                if (t >= boss.attack.duration) {
                    boss.attack.active = false;
                    bossAttackHitbox.visible = false;
                    boss.isHeavyAttack = false;

                    if (!playerHP.isDead()) {
                        boss.state = BossState::Chasing;
                        BossPlayLoop(gBossWalk);
                    }
                    else {
                        boss.state = BossState::Idle;
                        BossPlayLoop(gBossIdle);
                    }
                }


                break;
            }
            case BossState::Dead:
            default:
                bossAttackHitbox.visible = false;
                break;
            }
        }


        // --- animation update (player/boss) ---
        gAnimator->UpdateAnimation(deltaTime * gPlayerAnimSpeed);
        if (gBossAnimator)
            gBossAnimator->UpdateAnimation(deltaTime);

        // --- RENDER ---
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ========== MENU STATE ==========
        if (gGameState == GameState::Menu) {
            if (!gPrintedMenuMsg) {
                if (gBgmInit) {
                    ma_sound_stop(&gBgm);
                }
                std::cout << "=== MAIN MENU ===\n";
                std::cout << "Press ENTER to Start\n";
                gPrintedMenuMsg = true;
            }

            // Check for ENTER key to start game
            bool enterNow = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
            if (enterNow && !gPrevEnter) {
                // "TRY AGAIN" behavior: reset everything and start playing immediately
                ResetGame();
                gGameState = GameState::Playing;
                if (gBgmInit) {
                    ma_sound_start(&gBgm);
                }
                std::cout << "Retrying: Reset game and starting...\n";
            }
            gPrevEnter = enterNow;

            // Draw menu UI
            glDisable(GL_DEPTH_TEST);
            uiShader->use();
            glm::mat4 orthoProj = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);
            uiShader->setMat4("projection", orthoProj);

            // Dark overlay
            DrawRect(0, 0, SCR_WIDTH, SCR_HEIGHT, glm::vec3(0.02f, 0.02f, 0.03f));

            // Title background
            DrawRect(SCR_WIDTH / 2 - 250, SCR_HEIGHT / 2 + 50, 500, 100, glm::vec3(0.15f, 0.05f, 0.05f));
        
            // Game title text "DARK ARENA"
            DrawTextCenteredInBox("DARK ARENA", SCR_WIDTH / 2 - 250, SCR_HEIGHT / 2 + 50, 500, 100, 40, glm::vec3(0.9f, 0.7f, 0.2f));
        
            // "Press ENTER" box
            DrawRect(SCR_WIDTH / 2 - 200, SCR_HEIGHT / 2 - 80, 400, 60, glm::vec3(0.1f, 0.1f, 0.15f));

            // Pulsing effect for "Press ENTER"
            float pulse = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 3.0f);
            DrawRect(SCR_WIDTH / 2 - 190, SCR_HEIGHT / 2 - 70, 380, 40, glm::vec3(0.3f + pulse * 0.3f, 0.2f + pulse * 0.2f, 0.1f));
        
            // "START GAME" text
            DrawTextCenteredInBox("START GAME", SCR_WIDTH / 2 - 200, SCR_HEIGHT / 2 - 80, 400, 60, 28, glm::vec3(0.9f, 0.9f, 0.9f));

            glEnable(GL_DEPTH_TEST);

            UpdateSfxLifetime();

            glfwSwapBuffers(window);
            glfwPollEvents();
            continue; // Skip game logic
        }

        // ========== GAME OVER STATE ==========
        if (gGameState == GameState::GameOver) {
            if (!gPrintedGameOverMsg) {
                std::cout << "=== GAME OVER ===\n";
                std::cout << "Press ENTER to Return to Menu\n";
                gPrintedGameOverMsg = true;
            }

            // ENTER to retry immediately (behave like START GAME)
            bool enterNow = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
            if (enterNow && !gPrevEnter) {
                ResetGame();
                gGameState = GameState::Playing;

                if (gBgmInit) {
                    ma_sound_start(&gBgm);
                }
                std::cout << "Retrying: Reset game and starting...\n";
            }
            gPrevEnter = enterNow;

            // Draw game over UI
            glDisable(GL_DEPTH_TEST);
            uiShader->use();
            glm::mat4 orthoProj = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);
            uiShader->setMat4("projection", orthoProj);

            // Dark red overlay
            DrawRect(0, 0, SCR_WIDTH, SCR_HEIGHT, glm::vec3(0.1f, 0.01f, 0.01f));

            // Game Over title background
            DrawRect(SCR_WIDTH / 2 - 250, SCR_HEIGHT / 2 + 50, 500, 100, glm::vec3(0.3f, 0.05f, 0.05f));
        
            // "GAME OVER" text
            DrawTextCenteredInBox("GAME OVER", SCR_WIDTH / 2 - 250, SCR_HEIGHT / 2 + 50, 500, 100, 40, glm::vec3(0.9f, 0.2f, 0.2f));

            // "Press ENTER" box
            DrawRect(SCR_WIDTH / 2 - 200, SCR_HEIGHT / 2 - 80, 400, 60, glm::vec3(0.15f, 0.05f, 0.05f));

            // Pulsing effect
            float pulse = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 2.0f);
            DrawRect(SCR_WIDTH / 2 - 190, SCR_HEIGHT / 2 - 70, 380, 40, glm::vec3(0.4f + pulse * 0.2f, 0.1f, 0.1f));
        
            // "TRY AGAIN" text
            DrawTextCenteredInBox("TRY AGAIN", SCR_WIDTH / 2 - 200, SCR_HEIGHT / 2 - 80, 400, 60, 28, glm::vec3(0.9f, 0.9f, 0.9f));

            glEnable(GL_DEPTH_TEST);
            glfwSwapBuffers(window);
            glfwPollEvents();
            continue; // Skip game logic
        }

        // ========== VICTORY STATE ==========
        if (gGameState == GameState::Victory) {
            if (!gPrintedVictoryMsg) {
                std::cout << "=== VICTORY ===\n";
                std::cout << "Press ENTER to Play Again\n";
                gPrintedVictoryMsg = true;
            }

            // ENTER to restart immediately (same as START)
            bool enterNow = glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
            if (enterNow && !gPrevEnter) {
                ResetGame();
                gGameState = GameState::Playing;
                if (gBgmInit) {
                    ma_sound_start(&gBgm);
                }
                std::cout << "Restarting after victory...\n";
            }
            gPrevEnter = enterNow;

            // Draw victory UI (greenish)
            glDisable(GL_DEPTH_TEST);
            uiShader->use();
            glm::mat4 orthoProj = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);
            uiShader->setMat4("projection", orthoProj);

            // Dark overlay tinted green
            DrawRect(0, 0, SCR_WIDTH, SCR_HEIGHT, glm::vec3(0.05f, 0.08f, 0.02f));

            // Title background
            DrawRect(SCR_WIDTH / 2 - 250, SCR_HEIGHT / 2 + 50, 500, 100, glm::vec3(0.07f, 0.20f, 0.05f));
            // "VICTORY" text
            DrawTextCenteredInBox("VICTORY", SCR_WIDTH / 2 - 250, SCR_HEIGHT / 2 + 50, 500, 100, 40, glm::vec3(0.95f, 0.95f, 0.6f));

            // "Press ENTER" box
            DrawRect(SCR_WIDTH / 2 - 200, SCR_HEIGHT / 2 - 80, 400, 60, glm::vec3(0.1f, 0.16f, 0.08f));
            // Pulsing effect
            float pulseV = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 2.5f);
            DrawRect(SCR_WIDTH / 2 - 190, SCR_HEIGHT / 2 - 70, 380, 40, glm::vec3(0.2f + pulseV * 0.2f, 0.25f + pulseV * 0.2f, 0.08f));
            // "CONTINUE" text
            DrawTextCenteredInBox("TRY AGAIN", SCR_WIDTH / 2 - 200, SCR_HEIGHT / 2 - 80, 400, 60, 28, glm::vec3(0.95f, 0.95f, 0.95f));

            glEnable(GL_DEPTH_TEST);
            glfwSwapBuffers(window);
            glfwPollEvents();
            continue; // Skip game logic
        }

        // camera/projection
        glm::mat4 projection = glm::perspective(glm::radians(50.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 300.0f);
        glm::vec3 camPos; glm::mat4 view;
        ComputeCamera(camPos, view);

        // ========== ใช้ gShader กับทุกอย่าง  ==========
        gShader->use();
        gShader->setMat4("projection", projection);
        gShader->setMat4("view", view);



        // ---------- 1) วาดพื้น (ไม่มี bone → ใช้ identity) ----------
        glm::mat4 groundModel = glm::mat4(1.0f);
        gShader->setMat4("model", groundModel);

        if (groundTex != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, groundTex);
            gShader->setInt("texture_diffuse1", 0);
        }

        glBindVertexArray(groundVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // ---------- 2) วาด player (ใช้ bone ของ player) ----------
        // ดึง bone matrix ของ player มาใช้ทั้งวาดตัวละคร และผูกดาบ
        auto playerMats = gAnimator->GetFinalBoneMatrices();
        for (int i = 0; i < (int)playerMats.size(); ++i)
            gShader->setMat4("finalBonesMatrices[" + std::to_string(i) + "]", playerMats[i]);

        glm::mat4 playerModel = glm::mat4(1.0f);
        playerModel = glm::translate(playerModel, player.pos);
        playerModel = glm::rotate(playerModel, radiansf(player.yawDeg), glm::vec3(0, 1, 0));
        playerModel = glm::scale(playerModel, glm::vec3(1.0f));
        gShader->setMat4("model", playerModel);
        gModel->Draw(*gShader);



        // ---------- 3) วาดดาบติดมือขวา ----------
        if (gSwordModel && gSwordShader && gRightHandBoneIndex >= 0) {
            // 1) bone matrix จาก animator (อยู่ใน local model space)
            glm::mat4 handBone = playerMats[gRightHandBoneIndex];

            // 2) แปลงเป็น world space: model * bone
            glm::mat4 handWorld = playerModel * handBone;

            // 3) local offset ของดาบในมือ (ปรับทีหลังได้)
            glm::mat4 local = glm::mat4(1.0f);

            // เลื่อนจุดเริ่มต้นจากกระดูกนิดหน่อย (ลองเปลี่ยนจนกว่าจะไปแปะที่กำมือ)
            // ลองเริ่มแบบไม่มี translate ก่อน ถ้าอยากเช็กว่าจุดกระดูกอยู่ตรงไหน:
            // local = glm::translate(local, glm::vec3(0.0f, 0.0f, 0.0f));
            local = glm::translate(local, glm::vec3(-90.0f, 160.0f, 0.0f));

            // หมุนให้ดาบเอียงตาม grip มือ (ค่าคร่าว ๆ เอาไว้เริ่ม)
            //local = glm::rotate(local, glm::radians(-10.0f), glm::vec3(1, 1, 0));
            local = glm::rotate(local, glm::radians(-20.0f), glm::vec3(0, 0, 1));
            // ถ้าแกนไม่ตรง ลองขยับสามบรรทัดนี้ทีละแกน
            // local = glm::rotate(local, glm::radians(180.0f), glm::vec3(0, 1, 0));
            // local = glm::rotate(local, glm::radians(0.0f),   glm::vec3(1, 0, 0));

            // scale ดาบ
            local = glm::scale(local, glm::vec3(5.0f));

            // 4) matrix สุดท้ายของดาบ
            glm::mat4 swordModelMat = handWorld * local;

            // 5) วาดด้วย shader แบบ static
            gSwordShader->use();
            gSwordShader->setMat4("projection", projection);
            gSwordShader->setMat4("view", view);
            gSwordShader->setMat4("model", swordModelMat);

            gSwordModel->Draw(*gSwordShader);
        }




        // ---------- 4) วาด boss (ใช้ bone ของ boss) ----------
        gShader->use();
        gShader->setBool("uUseSkinning", true);

        if (!boss.hp.isDead()) {
            auto bossMats = gBossAnimator->GetFinalBoneMatrices();
            for (int i = 0; i < (int)bossMats.size(); ++i)
                gShader->setMat4("finalBonesMatrices[" + std::to_string(i) + "]", bossMats[i]);

            glm::mat4 bossModelMat = glm::mat4(1.0f);
            bossModelMat = glm::translate(bossModelMat, boss.pos);
            bossModelMat = glm::rotate(bossModelMat, radiansf(boss.yawDeg), glm::vec3(0, 1, 0));
            bossModelMat = glm::scale(bossModelMat, glm::vec3(BOSS_SCALE));
            gShader->setMat4("model", bossModelMat);

            gBossModel->Draw(*gShader);
        }

        // ---------- DEBUG HITBOX ----------
        if (showHitbox) {
            DrawHitbox(playerAttackHitbox);  // hitbox ท่าโจมตีผู้เล่น
            DrawHitbox(bossAttackHitbox);    // hitbox ท่าโจมตีบอส
            DrawHitbox(playerHitbox);
            DrawHitbox(boss.bodyHitbox);
        }

        // --- DRAW UI (Health Bars) ---
        glDisable(GL_DEPTH_TEST);

        uiShader->use();

        glm::mat4 orthoProj = glm::ortho(0.0f, (float)SCR_WIDTH, 0.0f, (float)SCR_HEIGHT);
        uiShader->setMat4("projection", orthoProj);

        // PLAYER HP -------------------------
        float pRatio = playerHP.ratio();

        // background
        DrawRect(50, 40, 200, 20, glm::vec3(0.2f, 0.0f, 0.0f));
        // hp
        DrawRect(50, 40, 200 * pRatio, 20, glm::vec3(0.8f, 0.1f, 0.1f));

        // BOSS HP ---------------------------
        float bRatio = boss.hp.ratio();
        if (!boss.hp.isDead()) {
            // background
            DrawRect(400, SCR_HEIGHT - 60, 400, 25, glm::vec3(0.2f, 0.0f, 0.0f));
            // hp
            DrawRect(400, SCR_HEIGHT - 60, 400 * bRatio, 25, glm::vec3(0.8f, 0.1f, 0.1f));
        }

        // POTION UI -------------------------
        // วาดเป็นกล่องเล็ก ๆ 5 อันที่มุมล่างซ้าย
        float boxSize = 18.0f;
        float startX = 50.0f;
        float startY = 15.0f;
        float gap = 4.0f;

        for (int i = 0; i < playerMaxPotions; ++i) {
            // ถ้ามีขวดเหลือ ตำแหน่งนี้จะเป็นขวดที่เติมได้ → สีสด
            bool hasPotion = (i < playerPotions);

            glm::vec3 col = hasPotion
                ? glm::vec3(0.9f, 0.8f, 0.2f)   // สีทอง/เหลือง น้ำยาเหลือ
                : glm::vec3(0.2f, 0.2f, 0.2f);  // สีเทา หมดแล้ว

            float x = startX + i * (boxSize + gap);
            float y = startY;

            DrawRect(x, y, boxSize, boxSize, col);
        }


        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }   // <<< ปิด while (!glfwWindowShouldClose)

    

    // cleanup
    if (groundTex) glDeleteTextures(1, &groundTex);
    if (groundVAO) { glDeleteVertexArrays(1, &groundVAO); glDeleteBuffers(1, &groundVBO); glDeleteBuffers(1, &groundEBO); }

    ShutdownAudio();   // <<<<< เพิ่มบรรทัดนี้

    return 0;

}

// ---------- Callbacks ----------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// เมาส์ควบคุมกล้อง (yaw/pitch)
void mouse_callback(GLFWwindow* /*window*/, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;
    lastX = xpos; lastY = ypos;

    cam.yawDeg += (float)xoffset * cam.sens * -1;
    cam.pitchDeg += (float)yoffset * cam.sens;
    if (cam.pitchDeg < cam.minPitch) cam.pitchDeg = cam.minPitch;
    if (cam.pitchDeg > cam.maxPitch) cam.pitchDeg = cam.maxPitch;
}

// สกอลล์เมาส์ซูม
void scroll_callback(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset) {
    cam.distance -= (float)yoffset * 0.5f;
    if (cam.distance < cam.minDist) cam.distance = cam.minDist;
    if (cam.distance > cam.maxDist) cam.distance = cam.maxDist;
}

void DrawLetter(float x, float y, float size, char letter, glm::vec3 color) {
    float w = size * 0.6f;  // letter width
    float h = size;         // letter height
    float t = size * 0.15f; // thickness

    // Draw small squares centered on the diagonal between two points
    auto draw_centered_stepped_diagonal = [&](float sx, float sy, float ex, float ey, int steps) {
        float stepSize = t * 0.9f;
        for (int s = 0; s < steps; ++s) {
            float u = (float)s / (float)(steps - 1);
            float px = sx + (ex - sx) * u;
            float py = sy + (ey - sy) * u;
            // center the small rect on (px,py)
            DrawRect(px - stepSize * 0.5f, py - stepSize * 0.5f, stepSize, stepSize, color);
        }
        };

    switch (letter) {
    case 'S':
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x, y + h / 2 - t / 2, w, t, color);
        DrawRect(x, y, w, t, color);
        DrawRect(x, y + h / 2, t, h / 2, color);
        DrawRect(x + w - t, y, t, h / 2, color);
        break;
    case 'T':
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x + w / 2 - t / 2, y, t, h, color);
        break;
    case 'A':
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x, y, t, h, color);
        DrawRect(x + w - t, y, t, h, color);
        DrawRect(x, y + h / 2 - t / 2, w, t, color);
        break;
    case 'R':
        DrawRect(x, y, t, h, color);                          // left stem
        DrawRect(x, y + h - t, w, t, color);                  // top bar
        DrawRect(x + w - t, y + h / 2, t, h / 2, color);      // top-right vertical
        DrawRect(x, y + h / 2 - t / 2, w, t, color);          // middle bar
        draw_centered_stepped_diagonal(x + w / 2, y + h / 2 - t / 2, x + w - t, y, 6); // diagonal leg
        break;
    case 'C':
        // Upper bar (inset slightly from full width so C looks open)
        DrawRect(x + t * 0.2f, y + h - t, w - t * 0.4f, t, color);
        // Left vertical stem (full height minus small caps)
        DrawRect(x, y + t, t, h - 2.0f * t, color);
        // Lower bar (inset like the top)
        DrawRect(x + t * 0.2f, y, w - t * 0.4f, t, color);
        break;
    case 'G':
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x, y, w, t, color);
        DrawRect(x, y, t, h, color);
        DrawRect(x + w - t, y, t, h, color);
        DrawRect(x + w / 2, y + h / 2 - t / 2, w / 2, t, color);
        break;
    case 'M':
        DrawRect(x, y, t, h, color);
        DrawRect(x + w - t, y, t, h, color);
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x + w / 2 - t / 2, y + h / 2, t, h / 2, color);
        break;
    case 'E':
        DrawRect(x, y, t, h, color);
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x, y + h / 2 - t / 2, w * 0.8f, t, color);
        DrawRect(x, y, w, t, color);
        break;
    case 'O':
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x, y, w, t, color);
        DrawRect(x, y, t, h, color);
        DrawRect(x + w - t, y, t, h, color);
        break;
    case 'V':
        DrawRect(x, y + h / 2, t, h / 2, color);
        DrawRect(x + w - t, y + h / 2, t, h / 2, color);
        DrawRect(x + t, y + h / 4, t, h / 4, color);
        DrawRect(x + w - 2 * t, y + h / 4, t, h / 4, color);
        DrawRect(x + w / 2 - t / 2, y, t, h / 4, color);
        break;
    case 'P':
        DrawRect(x, y, t, h, color);
        DrawRect(x, y + h - t, w, t, color);
        DrawRect(x + w - t, y + h / 2, t, h / 2, color);
        DrawRect(x, y + h / 2 - t / 2, w, t, color);
        break;
    case 'L':
        DrawRect(x, y, t, h, color);
        DrawRect(x, y, w, t, color);
        break;
    case 'Y':
        DrawRect(x, y + h / 2, t, h / 2, color);
        DrawRect(x + w - t, y + h / 2, t, h / 2, color);
        DrawRect(x + w / 2 - t / 2, y, t, h / 2, color);
        break;
    case 'N':
        DrawRect(x, y, t, h, color);                      // left stem
        DrawRect(x + w - t, y, t, h, color);              // right stem
        // diagonal top-left -> bottom-right
        draw_centered_stepped_diagonal(x + t * 0.5f, y + h - t * 0.5f, x + w - t * 0.5f, y + t * 0.5f, 12);
        break;
    case 'K':
        DrawRect(x, y, t, h, color);                   // left stem
        // Upper diagonal - from middle to top-right
        DrawRect(x + t, y + h / 2 - t / 2, w / 3, t, color); // mid segment
        DrawRect(x + w / 3, y + 2 * h / 3, w / 3, t, color);   // upper segment
        // Lower diagonal - from middle to bottom-right
        DrawRect(x + t, y + h / 2 - t / 2, w / 3, t, color); // mid segment (shared)
        DrawRect(x + w / 3, y + h / 6, w / 3, t, color);     // lower segment
        DrawRect(x + w - w / 4, y, t, h / 4, color);       // bottom tip
        DrawRect(x + w - w / 4, y + 3 * h / 4, t, h / 4, color); // top tip
        break;
    case 'W':
        DrawRect(x, y, t, h, color);
        DrawRect(x + w - t, y, t, h, color);
        DrawRect(x, y, w, t, color);
        DrawRect(x + w / 2 - t / 2, y, t, h / 2, color);
        break;
    case 'D':
        DrawRect(x, y, t, h, color);
        DrawRect(x, y + h - t, w * 0.7f, t, color);
        DrawRect(x, y, w * 0.7f, t, color);
        DrawRect(x + w * 0.7f, y + t, t, h - 2 * t, color);
        break;
    case 'I':
        // Top cap
        DrawRect(x, y + h - t, w, t, color);
        // Bottom cap
        DrawRect(x, y, w, t, color);
        // Vertical stem centered
        DrawRect(x + w / 2 - t / 2, y, t, h, color);
        break;
    case ' ':
        break;
    default:
        break;
    }
}
void DrawTextUI(const char* text, float x, float y, float size, glm::vec3 color) {
    float spacing = size * 0.8f;
    float currentX = x;
    
    for(int i = 0; text[i] != '\0'; i++) {
        DrawLetter(currentX, y, size, text[i], color);
        currentX += spacing;
    }
}

