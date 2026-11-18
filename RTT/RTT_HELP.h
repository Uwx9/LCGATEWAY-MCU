#ifndef RTT_HELP_H
#define RTT_HELP_H

/* 通过 RTT 打印一次任务、NVIC 中断和内存快照，tag 用于区分调用时机 */
void report_system_state(const char *tag);

/* 创建轮询 RTT 命令的低优先级任务，调度器启动前调用 */
void rtt_cmd_task_start(void);

#endif /* RTT_HELP_H */
