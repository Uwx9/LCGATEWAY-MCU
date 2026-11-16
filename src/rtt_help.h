#ifndef RTT_HELP_H
#define RTT_HELP_H

/* 通过 RTT 打印一次任务和 NVIC 中断快照，tag 用于区分调用时机 */
void report_system_state(const char *tag);

#endif /* RTT_HELP_H */
