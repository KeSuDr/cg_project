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

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
struct Player {
    glm::vec3 pos{ 0.0f, 0.0f, 0.0f };
    float yawDeg = 0.0f;          // หมุนตัวละครรอบแกน Y
    float moveSpeed = 3.4f;       // m/s (walking)
    float runSpeed = 6.0f;        // m/s (running)
    float rollSpeed = 2.0f;       // m/s
    float height = 1.0f;          // ความสูงศีรษะโดยประมาณ

    // Jump state
    bool isGrounded = true;
    float yVelocity = 0.0f;
} player;

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

struct Hitbox {
    glm::vec3 center; // world-space center
    glm::vec3 halfExtents; // ครึ่งขนาดแต่ละแกน
    bool visible = true; // debug draw

    // ตรวจสอบการชนแบบ AABB กับ hitbox อื่น
    bool intersects(const Hitbox& other) const {
        return std::abs(center.x - other.center.x) <= (halfExtents.x + other.halfExtents.x) &&
            std::abs(center.y - other.center.y) <= (halfExtents.y + other.halfExtents.y) &&
            std::abs(center.z - other.center.z) <= (halfExtents.z + other.halfExtents.z);
    }
};

Hitbox playerHitbox;
GLuint hitboxVAO = 0, hitboxVBO = 0, hitboxEBO = 0;
Shader* hitboxShader = nullptr;

// ---------- Timing ----------
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ---------- Mouse state ----------
bool firstMouse = true;
double lastX = SCR_WIDTH / 2.0;
double lastY = SCR_HEIGHT / 2.0;

// ---------- Input edges ----------
bool prevLMB = false;
bool prevSpace = false;
bool prevE = false; // jump key edge
bool prevShift = false; // run key edge

// ---------- Animation State ----------
enum class ActionState { Idle, Moving, Running, Rolling, Attacking, Jumping };
ActionState state = ActionState::Idle;
float actionTimeLeft = 0.0f;

// ---------- GL / Content ----------
Shader* gShader = nullptr;
Model* gModel = nullptr;

Animation* gIdle = nullptr, * gWalk = nullptr, * gRun = nullptr, * gRoll = nullptr, * gAttack = nullptr, * gJump = nullptr;
Animator* gAnimator = nullptr;

// Ground geometry
GLuint groundVAO = 0, groundVBO = 0, groundEBO = 0;
unsigned int groundTex = 0; // id for ground texture

// ----- helpers -----
static inline float radiansf(float d) { return d * 0.017453292519943295f; }

void PlayLoop(Animation* anim) {
    gAnimator->PlayAnimation(anim);
}
void PlayOneShot(Animation* anim, float& outSec) {
    gAnimator->PlayAnimation(anim);
    float durTicks = anim->GetDuration();
    float tps = anim->GetTicksPerSecond();
    outSec = (tps > 0.0f) ? durTicks / tps : 0.7f; // fallback
}

// เวกเตอร์ forward/right “ตามกล้อง” (ใช้กับ WASD)
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

int main() {
    // ---- GLFW/GL setup ----
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Souls-like TPS (Mouse Camera)", NULL, NULL);
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

    // ---- Shaders ----
    Shader ourShader("anim_model.vs", "anim_model.fs");
    gShader = &ourShader;

    // หลังจาก gShader โหลดเสร็จ
    hitboxShader = new Shader(
        "hitbox.vs", // vertex shader
        "hitbox.fs"  // fragment shader
    );
    std::cout << FileSystem::getPath("anim_model.fs") << "\n";

    // ---- Load Model & Animations ----
    Model  ourModel(FileSystem::getPath("resources/objects/models/idle.dae"));
    gModel = &ourModel;

    Animation idleAnim(FileSystem::getPath("resources/objects/models/idle.dae"), &ourModel);
    Animation walkAnim(FileSystem::getPath("resources/objects/models/walk.dae"), &ourModel);
    Animation walkBackwardAnim(FileSystem::getPath("resources/objects/models/walk_backward.dae"), &ourModel);
    Animation runAnim(FileSystem::getPath("resources/objects/models/run.dae"), &ourModel);
    Animation strafeLeftAnim(FileSystem::getPath("resources/objects/models/strafe_left.dae"), &ourModel);
    Animation strafeRightAnim(FileSystem::getPath("resources/objects/models/strafe_right.dae"), &ourModel);
    Animation rollAnim(FileSystem::getPath("resources/objects/models/roll.dae"), &ourModel);
    Animation attackAnim(FileSystem::getPath("resources/objects/models/attack.dae"), &ourModel);
    Animation jumpAnim(FileSystem::getPath("resources/objects/models/jump.dae"), &ourModel);

    gIdle = &idleAnim;
    gWalk = &walkAnim;
    gRun = &runAnim;
    gRoll = &rollAnim;
    gAttack = &attackAnim;
    gJump = &jumpAnim;

    Animator animator(gIdle);
    gAnimator = &animator;

    // ---- Ground ----
    CreateGround();

    // ---- Load ground texture (change path if needed) ----
    std::string groundTexPath = FileSystem::getPath("resources/objects/models/textures/ground.png");
    groundTex = LoadTexture(groundTexPath);
    if (groundTex == 0) {
        std::cout << "Warning: ground texture not loaded, ground will still draw with shader default.\n";
    }

    // NOTE:
    // We DO NOT forcibly set a global sampler name like "diffuseTexture" (which caused issues).
    // For drawing the ground we will set the sampler that your fragment shader expects:
    // Your anim_model.fs uses: uniform sampler2D texture_diffuse1;
    // We'll set that before drawing the ground only (Model::Draw is expected to set its own sampler uniforms).

    // toggle hitbox ก่อน main loop
    CreateHitboxMesh();
    bool showHitbox = true;

    // -------- Main loop --------
    while (!glfwWindowShouldClose(window)) {
        // --- timing ---
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // --- input ---
        glm::vec2 moveInput(0.0f);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveInput.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveInput.y -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveInput.x += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveInput.x -= 1.0f;

        bool spaceNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        bool lmbNow = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        bool shiftNow = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;

        // toggle hitbox with H (edge detect)
        if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS && !prevLMB) showHitbox = !showHitbox;
        prevLMB = (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS);

        // --- update player state, gravity, movement ---
        // ===== STATE MACHINE =====
        if (state == ActionState::Rolling || state == ActionState::Attacking) {
            actionTimeLeft -= deltaTime;
            if (actionTimeLeft <= 0.0f) {
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
            }
            else if (spaceNow && !prevSpace) {
                state = ActionState::Rolling;  PlayOneShot(gRoll, actionTimeLeft);
            }
            else if (lmbNow && !prevLMB) {
                state = ActionState::Attacking; PlayOneShot(gAttack, actionTimeLeft);
            }
            else {
                // If moving and holding shift -> running
                if (glm::length(moveInput) > 0.0f) {
                    if (shiftNow) {
                        if (state != ActionState::Running) { state = ActionState::Running; PlayLoop(gRun); }
                    }
                    else {
                        if (state != ActionState::Moving) { state = ActionState::Moving; PlayLoop(gWalk); }
                    }
                }
                else {
                    if (state != ActionState::Idle) { state = ActionState::Idle;   PlayLoop(gIdle); }
                }
            }
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
            // กลิ้งพุ่งไปข้างหน้า “ตามทิศตัวละคร” (ไม่ใช่ทิศกล้อง)
            glm::vec3 forwardChar = glm::normalize(glm::vec3(std::sin(radiansf(player.yawDeg)), 0, std::cos(radiansf(player.yawDeg))));
            player.pos += forwardChar * player.rollSpeed * deltaTime;
        }

        prevSpace = spaceNow;
        prevLMB = lmbNow;
        prevE = eNow;
        prevShift = shiftNow;

        playerHitbox.center = player.pos + glm::vec3(0, player.height / 1.2 5f, 0);
        playerHitbox.halfExtents = glm::vec3(0.3f, player.height, 0.3f);


        // --- animation update ---
        gAnimator->UpdateAnimation(deltaTime);

        // --- RENDER ---
        glClearColor(0.06f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // camera/projection
        glm::mat4 projection = glm::perspective(glm::radians(50.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 300.0f);
        glm::vec3 camPos; glm::mat4 view;
        ComputeCamera(camPos, view);

        // ----- draw ground -----
        gShader->use();
        gShader->setMat4("projection", projection);
        gShader->setMat4("view", view);
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
        glActiveTexture(GL_TEXTURE0);

        // ----- draw hitbox -----
        if (showHitbox) {
            hitboxShader->use();
            glDisable(GL_DEPTH_TEST);
            glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
            glEnable(GL_DEPTH_TEST);

            glm::mat4 hbModel = glm::mat4(1.0f);
            hbModel = glm::translate(hbModel, playerHitbox.center);
            hbModel = glm::scale(hbModel, playerHitbox.halfExtents * 2.0f);

            hitboxShader->setMat4("model", hbModel);
            hitboxShader->setMat4("view", view);
            hitboxShader->setMat4("projection", projection);

            glBindVertexArray(hitboxVAO);
            glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            glEnable(GL_DEPTH_TEST);
        }


        // ----- draw character -----
        gShader->use(); // ✅ สำคัญ! ต้องเรียก shader ของโมเดลอีกครั้ง
        gShader->setMat4("projection", projection);
        gShader->setMat4("view", view);

        // bone matrices
        auto transforms = gAnimator->GetFinalBoneMatrices();
        for (int i = 0; i < (int)transforms.size(); ++i)
            gShader->setMat4("finalBonesMatrices[" + std::to_string(i) + "]", transforms[i]);

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, player.pos);
        model = glm::rotate(model, radiansf(player.yawDeg), glm::vec3(0, 1, 0));
        model = glm::scale(model, glm::vec3(1.0f)); // ใช้ scale 1.0f
        gShader->setMat4("model", model);

        gModel->Draw(*gShader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }


    // cleanup
    if (groundTex) glDeleteTextures(1, &groundTex);
    if (groundVAO) { glDeleteVertexArrays(1, &groundVAO); glDeleteBuffers(1, &groundVBO); glDeleteBuffers(1, &groundEBO); }

    glfwTerminate();
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
