/**
 * @brief 图像处理扩展接口（可选使用）
 * @author achene
 * @date 2025-08-05
 * 
 *  - 不继承或不设置时，不影响编码推流的基本功能
 *  - 继承并实现processFrame()可自定义图像处理逻辑,processFrame提供 BGR24 格式cv::Mat数据，
 *    直接使用opencv处理图像，需要保证本地操作或处理后的数据拷贝给提供的cv::Mat从而保证后续处
 *    理正常执行
 */

#pragma once
#include <stdint.h>
#include <opencv2/opencv.hpp>

class ImageProcessor {
public:
    /**
     * @brief 虚析构函数
     * @details 确保指向子类的父类指针能正确释放子类对象
     */
    virtual ~ImageProcessor() = default;
    
    /**
     * @brief 默认构造函数
     */
    ImageProcessor() = default;
    
    /**
     * @brief 禁止拷贝构造函数
     * @details 通常不希望接口类被拷贝（可能包含资源）
     */
    ImageProcessor(const ImageProcessor&) = delete;
    
    /**
     * @brief 禁止拷贝赋值运算符
     * @details 通常不希望接口类被拷贝（可能包含资源）
     */
    ImageProcessor& operator=(const ImageProcessor&) = delete;
    
    /**
     * @brief 允许移动构造函数
     */
    ImageProcessor(ImageProcessor&&) = default;
    
    /**
     * @brief 允许移动赋值运算符
     */
    ImageProcessor& operator=(ImageProcessor&&) = default;

    /**
     * @brief 初始化接口
     * @details 提供默认空实现，子类可重写实现初始化逻辑
     */
    virtual void init() {}
    
    /**
     * @brief 帧处理接口
     * @param mat 输入输出参数，BGR24格式的图像数据cv::Mat对象
     * @details 用户需在该函数中进行本地图像处理操作，处理后的数据需拷贝给输入的mat参数，
     *          以确保后续处理流程能正常使用处理后的图像数据
     */
    virtual void processFrame(cv::Mat& mat) {}
    
    /**
     * @brief 清理接口
     * @details 提供默认空实现，子类可重写实现资源清理逻辑
     */
    virtual void cleanup() {}
};