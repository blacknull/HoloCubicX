#include "imu.h"
#include "common.h"

const char *active_type_info[] = {"TURN_RIGHT", "RETURN",
                                  "TURN_LEFT", "UP",
                                  "DOWN", "GO_FORWORD",
                                  "SHAKE", "UNKNOWN"};

IMU::IMU()
{
    present = false;
    action_info.isValid = false;
    action_info.active = ACTIVE_TYPE::UNKNOWN;
    action_info.long_time = true;
    for (int pos = 0; pos < ACTION_HISTORY_BUF_LEN; ++pos)
    {
        act_info_history[pos] = UNKNOWN;
    }
    act_info_history_ind = ACTION_HISTORY_BUF_LEN - 1;
    this->order = 0;
    last_gesture_ms = 0;
    for (int a = 0; a < 3; ++a)
    {
        g_idx[a] = 0;
        g_filled[a] = 0;
        g_sum[a] = 0;
        g_sum_sq[a] = 0;
    }
}

void IMU::pushGyro(int axis, int16_t v)
{
    if (g_filled[axis] == WIN)
    {
        int16_t old = g_buf[axis][g_idx[axis]];
        g_sum[axis]    -= old;
        g_sum_sq[axis] -= (int64_t)old * old;
    }
    else
    {
        g_filled[axis]++;
    }
    g_buf[axis][g_idx[axis]] = v;
    g_sum[axis]    += v;
    g_sum_sq[axis] += (int64_t)v * v;
    g_idx[axis] = (g_idx[axis] + 1) % WIN;
}

float IMU::gyroMean(int axis)
{
    return g_filled[axis] ? (float)g_sum[axis] / g_filled[axis] : 0.f;
}

float IMU::gyroStd(int axis)
{
    if (g_filled[axis] < 2) return 0.f;
    float m = gyroMean(axis);
    float ex2 = (float)g_sum_sq[axis] / g_filled[axis];
    float v = ex2 - m * m;
    if (v < 0) v = 0;
    return sqrtf(v);
}

bool IMU::writeReg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool IMU::readBytes(uint8_t reg, uint8_t *buf, uint8_t len)
{
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(MPU_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
    return true;
}

void IMU::init(uint8_t order, uint8_t auto_calibration,
               SysMpuConfig *mpu_cfg)
{
    this->setOrder(order);
    Wire.begin(IMU_I2C_SDA, IMU_I2C_SCL, 400000);
    Wire.setTimeOut(50);

    // PWR_MGMT_1: 唤醒
    if (!writeReg(0x6B, 0x00))
    {
        Serial.println(F("Unable to connect to MPU6050."));
        present = false;
        return;
    }
    writeReg(0x1B, 0x00); // GYRO_CONFIG: ±250 dps
    writeReg(0x1C, 0x00); // ACCEL_CONFIG: ±2 g
    writeReg(0x1A, 0x03); // CONFIG: DLPF ~44 Hz

    // WHO_AM_I sanity
    uint8_t who = 0;
    if (!readBytes(0x75, &who, 1))
    {
        Serial.println(F("MPU6050 WHO_AM_I read failed."));
        present = false;
        return;
    }
    Serial.printf("[MPU] WHO_AM_I=0x%02X\n", who);
    if (who != 0x68 && who != 0x70 && who != 0x72)
    {
        present = false;
        return;
    }
    present = true;
    Serial.println(F("Initialization MPU6050 success."));
}

void IMU::setOrder(uint8_t order)
{
    this->order = order;
}

bool IMU::Encoder_GetIsPush(void)
{
#ifdef PEAK
    return (digitalRead(CONFIG_ENCODER_PUSH_PIN) == LOW);
#else
    return false;
#endif
}

void IMU::getVirtureMotion6(ImuAction *action_info)
{
    if (!present)
    {
        action_info->v_ax = action_info->v_ay = action_info->v_az = 0;
        action_info->v_gx = action_info->v_gy = action_info->v_gz = 0;
        return;
    }

    uint8_t b[14];
    if (!readBytes(0x3B, b, 14))
    {
        action_info->v_ax = action_info->v_ay = action_info->v_az = 0;
        action_info->v_gx = action_info->v_gy = action_info->v_gz = 0;
        return;
    }
    action_info->v_ax = (int16_t)((b[0]  << 8) | b[1]);
    action_info->v_ay = (int16_t)((b[2]  << 8) | b[3]);
    action_info->v_az = (int16_t)((b[4]  << 8) | b[5]);
    action_info->v_gx = (int16_t)((b[8]  << 8) | b[9]);
    action_info->v_gy = (int16_t)((b[10] << 8) | b[11]);
    action_info->v_gz = (int16_t)((b[12] << 8) | b[13]);

    if (order & X_DIR_TYPE)
    {
        action_info->v_ax = -action_info->v_ax;
        action_info->v_gx = -action_info->v_gx;
    }
    if (order & Y_DIR_TYPE)
    {
        action_info->v_ay = -action_info->v_ay;
        action_info->v_gy = -action_info->v_gy;
    }
    if (order & Z_DIR_TYPE)
    {
        action_info->v_az = -action_info->v_az;
        action_info->v_gz = -action_info->v_gz;
    }
    if (order & XY_DIR_TYPE)
    {
        int16_t swap_tmp;
        swap_tmp = action_info->v_ax;
        action_info->v_ax = action_info->v_ay;
        action_info->v_ay = swap_tmp;
        swap_tmp = action_info->v_gx;
        action_info->v_gx = action_info->v_gy;
        action_info->v_gy = swap_tmp;
    }
}

ImuAction *IMU::getAction(void)
{
    // 基于陀螺仪的突变检测：对每个轴维护滑动窗口的均值与标准差，
    // 当 |x - mean| > k*sigma 且超过绝对下限时，判定为手势。
    // 这种方式不受重力常量影响，只响应实际的旋转事件。
    static const float   T_THRESHOLD  = 4.0f;   // k * sigma
    static const int16_t ABS_FLOOR    = 6000;   // ~45 dps @ 131 LSB/dps
    static const uint32_t COOLDOWN_MS = 500;

    ImuAction tmp_info;
    getVirtureMotion6(&tmp_info);
    tmp_info.active = ACTIVE_TYPE::UNKNOWN;

    int16_t g[3] = { tmp_info.v_gx, tmp_info.v_gy, tmp_info.v_gz };
    uint32_t now = (uint32_t)GET_SYS_MILLIS();

    bool ready = (g_filled[0] == WIN && g_filled[1] == WIN && g_filled[2] == WIN);
    if (ready && (now - last_gesture_ms) > COOLDOWN_MS)
    {
        int best = -1;
        float best_t = 0;
        for (int i = 0; i < 3; ++i)
        {
            float diff = g[i] - gyroMean(i);
            float sd = gyroStd(i);
            if (sd < 50.0f) sd = 50.0f; // 防止静止时分母过小
            float t = diff / sd;
            if (fabsf(t) > fabsf(best_t) && fabsf(diff) > ABS_FLOOR)
            {
                best_t = t;
                best = i;
            }
        }
        if (best >= 0 && fabsf(best_t) > T_THRESHOLD)
        {
            switch (best)
            {
            case 0: // gx (俯仰)：前/后倾 → 进入/退出
                tmp_info.active = (best_t > 0) ? ACTIVE_TYPE::DOWN_MORE
                                               : ACTIVE_TYPE::RETURN;
                break;
            case 1: // gy (横滚)：左/右倾 → 切换 APP
                tmp_info.active = (best_t > 0) ? ACTIVE_TYPE::TURN_RIGHT
                                               : ACTIVE_TYPE::TURN_LEFT;
                break;
            case 2: // gz (偏航)：旋转 → SHAKE
                tmp_info.active = ACTIVE_TYPE::SHAKE;
                break;
            }
            last_gesture_ms = now;
        }
    }

    for (int i = 0; i < 3; ++i) pushGyro(i, g[i]);

    if (!action_info.isValid && ACTIVE_TYPE::UNKNOWN != tmp_info.active)
    {
        action_info.isValid = 1;
        action_info.active = tmp_info.active;
    }
    return &action_info;
}
