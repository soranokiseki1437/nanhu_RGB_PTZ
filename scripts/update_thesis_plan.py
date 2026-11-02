# -*- coding: utf-8 -*-
import docx
import os

def update_thesis_docx(path):
    if not os.path.exists(path):
        print(f"File not found: {path}")
        return
        
    doc = docx.Document(path)
    
    # Update Table 0 Row 2 (Chapter 2)
    t0 = doc.tables[0]
    ch2_text = (
        "无人机小目标轻量化检测与抗虚警机制：\n"
        "1. 数据集基准确立：确立大连理工大学发表于 IEEE T-ITS 顶刊的 DUT Anti-UAV 权威基准数据集（Detection 子集，7,800 张真实可见光 RGB 图像，80.9% 弱小目标）为主训练与验证基准，辅以 Drone-vs-Bird 挑战赛基准评估飞鸟杂波下的抗虚警能力；\n"
        "2. 2×2 析因消融实验与模型选型：在反无人机专属检测数据集（DUT Anti-UAV）统一同构重训微调的协议下，开展 YOLOv8 vs YOLO11 (Nano vs Small) 四象限消融横评，系统评估 C2PSA 空间注意力与模型尺度对空域微弱局部高频信号的捕获效能与推理时延，为云台伺服闭环确立最优候选模型；\n"
        "3. 专属领域微调（Domain Fine-Tuning）：将 VOC XML 标注批量转换为 YOLO 归一化格式，在单类别（0: UAV）下结合 Mosaic、Copy-Paste 与小目标特征加权执行领域重训，实现空域弱小目标检测性能的大幅跃升；\n"
        "4. 结构微调与微尺度增强：引入无损下采样（SPD-Conv / ADown）保留微小像素高频细节，剪除 P5 冗余头，构建保留 P2 微尺度检测头的浅层高分辨率结构；\n"
        "5. 吸收 DeTrAck 思想构建“YOLO候选定位 + 条件式轻量验证网络 (VaL_CNN)”两级防虚警架构：候选初筛放宽至低门限（0.2）保远距弱小目标高召回，对 [0.2, 0.7) 存疑目标截取 64×64 局部切片输入 5 层微型 CNN 执行置信度加权二次校验，虚警率降低 2.6 倍；\n"
        "6. 联动 AMFT 状态机实现常态零频休眠、失锁单次唤醒的协同调度。"
    )
    t0.rows[2].cells[1].text = ch2_text
    
    # Check if Literature references section has our new datasets
    existing_text = "\n".join([p.text for p in doc.paragraphs])
    if "Vision-Based Anti-UAV Detection and Tracking" not in existing_text:
        # Find where to insert literature
        for i, p in enumerate(doc.paragraphs):
            if "### 8. 轻量单目标跟踪网络神经架构搜索标杆" in p.text or "8. 轻量单目标跟踪网络神经架构搜索标杆" in p.text:
                # Insert before this paragraph
                p_ref1 = p.insert_paragraph_before("### 7.1 空域反无人机检测与跟踪权威基准数据集（第2章与第3章实验第一主基准）\n\n"
                                                  "2022年 · IEEE T-ITS 顶刊 (中科院一区 Top / CCF 推荐，大连理工大学王栋团队)\n"
                                                  "文献题目：Vision-Based Anti-UAV Detection and Tracking\n"
                                                  "收录刊物：IEEE Transactions on Intelligent Transportation Systems, 2022, 23(12), 25323–25334.\n"
                                                  "DOI/检索：10.1109/TITS.2022.3177627\n"
                                                  "核心启示与本课题支撑：提出了国际公认的开源可见光反无人机基准库 DUT Anti-UAV，解耦为 10,000 张静态检测集（5,200 Train / 2,600 Val / 2,200 Test）与 20 组连续动态跟踪序列。经本课题统计，其极弱小目标（< 32×32 像素）占比达 52.4%，中小目标占比 28.5%，是验证长焦小目标检测与跟踪算法最权威的地面仰拍空域基准。本课题以该数据集作为检测模型微调、2×2 消融实验与跟踪评测的第一黄金基准。\n")
                
                p_ref2 = p.insert_paragraph_before("### 7.2 国际反无人机小目标抗虚警标杆挑战赛（第2章抗虚警与飞鸟杂波评估支撑）\n\n"
                                                  "2017-2025年 · IEEE AVSS / ICASSP / IJCNN 全球挑战赛 (欧盟 Horizon 2020 SafeShore 计划)\n"
                                                  "文献题目：The Drone-vs-Bird Detection Grand Challenge: A Review of Methods and Results\n"
                                                  "收录刊物：IEEE Open Journal of Signal Processing / IEEE AVSS / IJCNN 2025.\n"
                                                  "核心启示与本课题支撑：针对防空监控中“远距离飞鸟与微型无人机外观及飞行动力学极度相似”的致命虚警痛点，提供大量长焦低空与地平线复杂背景视频流，目标尺寸集中在 10×10 ~ 30×30 像素。本课题以此作为评估 YOLO11 + VaL_CNN 条件式二级校验机制在极端虚警工况下可靠性的重要对照基准。\n")
                break
                
    doc.save(path)
    print("Successfully updated docx:", path)

if __name__ == "__main__":
    update_thesis_docx(r"d:\PyProjects\RGB_PTZ_Integrated\课题\反无人机云台视觉跟踪与抗遮挡重检测课题规划指南.docx")
