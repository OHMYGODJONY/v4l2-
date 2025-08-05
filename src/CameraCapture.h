#pragma once
/**
 * @file CameraCapture.h
 * @class CameraCapture
 * @brief V4L2摄像头采集
 * @author achene
 * @date 2025-08-05
 * 
 * 该模块封装了Linux下基于V4L2 (Video for Linux 2) API的摄像头采集功能，
 * 支持配置摄像头参数（分辨率、帧率、像素格式等）、初始化设备、启动/停止采集，
 * 以及通过回调函数机制获取摄像头帧数据。
 * 
 * 主要功能特点：
 * - 支持多种像素格式（默认YUYV）
 * - 基于DMA缓冲区实现高效数据传输
 * - 多线程异步采集模式
 * - 帧数据通过回调函数实时推送
 * - 包含完整的设备初始化、缓冲区管理和资源释放逻辑
 * 
 * 使用流程：
 * 1. 构造CameraCapture对象并指定设备路径及采集参数
 * 2. 调用initialize()初始化设备
 * 3. 通过set_frame_callback()设置帧处理回调函数
 * 4. 调用start()开始采集
 * 5. 采集完成后调用stop()停止采集
 * 6. 对象析构时自动释放相关资源
 */
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <linux/videodev2.h>
#include <thread>

// 摄像头帧数据结构
struct CameraFrame {
    int camera_id;          // 摄像头标识
    int buf_index;          // buf 索引
    int fd;                 // DMA-BUF文件描述符
    void* data;             // 内存映射地址指针
    size_t length;          // 数据长度
    size_t bytes_used;      // 实际使用字节数
    uint32_t width;         // 图像宽度
    uint32_t height;        // 图像高度
    uint32_t stride;        // 步长
    uint32_t pixel_format;  // 像素格式 (V4L2_PIX_FMT_*)
    timeval timestamp;      // 时间戳
    uint32_t sequence;      // 帧序列号
    std::function<void()> return_buffer; //释放缓冲回调
};

class CameraCapture {
public:
    // 回调函数类型定义
    using FrameCallback = std::function<void(const CameraFrame&)>;
    
    /**
     * @brief 构造函数，初始化采集参数
     * @param device_path 摄像头设备路径（如/dev/video0）
     * @param width 采集宽度（像素）
     * @param height 采集高度（像素）
     * @param fps 帧率（帧/秒）
     * @param pixel_format 像素格式（默认YUYV）
     */
    CameraCapture(const std::string& device_path, 
                 uint32_t width, 
                 uint32_t height, 
                 uint32_t fps,
                 uint32_t pixel_format = V4L2_PIX_FMT_YUYV);
    
    /**
     * @brief 析构函数，释放所有资源
     * 自动调用stop()停止采集，释放缓冲区，关闭设备文件描述符
     */
    ~CameraCapture();
    
    /**
     * @brief 初始化摄像头设备
     * 流程：打开设备 -> 检查设备能力 -> 设置像素格式、分辨率、帧率 -> 申请缓冲区 -> 映射缓冲区
     * @return 成功返回true，失败返回false
     */
    bool initialize();
    
    /**
     * @brief 开始采集
     * 启动采集线程，线程中循环获取帧数据并通过回调函数推送
     * 注意：需先调用initialize()并返回成功后才能调用此函数
     */
    void start();
    
    /**
     * @brief 停止采集
     * 停止采集线程，暂停帧数据获取
     */
    void stop();
    
    /**
     * @brief 设置帧数据回调函数（左值引用版本和右值引用版本，支持移动语义）
     * @param callback 回调函数对象，当有新帧到达时会被调用
     */
    void set_frame_callback(const FrameCallback &callback); 
    void set_frame_callback(FrameCallback &&callback);
    
    /**
     * @brief 获取当前采集状态
     * @return 正在采集返回true，否则返回false
     */
    bool is_running() const { return running_; }
    
    /**
     * @brief 设置摄像头ID（多摄像头场景下使用）
     * @param id 摄像头标识ID
     */
    void set_camera_id(int id) { camera_id_ = id; }

    /**
     * @brief 获取当前摄像头ID
     * @return 摄像头标识ID
     */
    int get_camera_id() const { return camera_id_; }

private:
    /**
     * @brief 采集线程主函数
     * 循环执行：获取帧数据 -> 调用回调函数推送帧 -> 等待下一帧（直到stop()被调用）
     */
    void capture_thread();
    
    /**
     * @brief 初始化V4L2设备核心流程
     * 包括：打开设备文件、检查设备是否支持视频捕获、设置视频格式（分辨率/像素格式）、设置帧率
     * @return 成功返回true，失败返回false
     */
    bool init_device();
    
    /**
     * @brief 卸载设备资源
     * 包括：解除内存映射、释放缓冲区、关闭设备文件描述符
     */
    void uninit_device();
    
    /**
     * @brief 向V4L2驱动请求DMA缓冲区
     * 申请多个缓冲区并进行内存映射，用于高效接收摄像头数据（零拷贝）
     * @return 成功返回true，失败返回false
     */
    bool request_buffers();
    
    /**
     * @brief 启动视频流传输
     * 通知V4L2驱动开始向缓冲区填充帧数据
     * @return 成功返回true，失败返回false
     */
    bool start_streaming();
    
    /**
     * @brief 停止视频流传输
     * 通知V4L2驱动停止向缓冲区填充数据
     * @return 成功返回true，失败返回false
     */
    bool stop_streaming();
    
    /**
     * @brief 从设备获取一帧数据
     * 阻塞等待直到有新帧到达，或超时/出错
     * @param frame 输出参数，用于存储获取到的帧数据
     * @return 成功返回true，失败返回false
     */
    bool get_frame(CameraFrame& frame);
    
    /**
     * @brief 将缓冲区归还到设备队列
     * 帧数据处理完成后需调用此函数，让缓冲区重新参与数据采集循环
     * @param index 缓冲区索引
     * @return 成功返回true，失败返回false
     */
    bool return_buffer_to_queue(int index);
    
    /**
     * @brief 导出DMA-BUF文件描述符
     * 将V4L2缓冲区导出为DMA-BUF，用于跨进程或硬件加速模块共享数据
     * @param index 缓冲区索引
     * @return 成功返回DMA-BUF的文件描述符，失败返回-1
     */
    int export_dma_buf(int index);

private:
    // 配置参数
    std::string device_path_;
    uint32_t width_;
    uint32_t height_;
    uint32_t stride_; // 保存步长
    uint32_t fps_;
    uint32_t pixel_format_;
    int camera_id_ = 0;  // 默认摄像头ID为0
    
    // 设备状态
    int fd_ = -1;  // 设备文件描述符
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // 线程控制
    std::unique_ptr<std::thread> capture_thread_;
    
    // 缓冲区管理
    struct Buffer {
        void* start;
        size_t length;
        int dma_fd;  // DMA-BUF文件描述符
    };
    std::vector<Buffer> buffers_;
    
    // 回调函数
    FrameCallback frame_callback_;
    
    /**
     * @brief 错误处理函数
     * 打印错误信息（可扩展为日志输出）
     * @param message 错误描述信息
     */
    void report_error(const std::string& message);
};
