# kjnu0522001-pintos

## 프로젝트 개요
A Pintos project for Kongju National University's Operating Systems course 0522001, configured to build and run on modern Ubuntu/QEMU environments.

80x86 아키텍처용 교육용 운영체제인 Pintos는 커널 스레드, 사용자 프로그램 적재·실행, 파일 시스템 등을 실제 상용 OS보다 단순화해 학습용으로 제공한다. Project #1에서는 물리 하드웨어 대신 오픈 소스 하드웨어 에뮬레이터인 QEMU(Quick Emulator)를 활용해 가상 머신 환경에서 Pintos를 구동한다.

Original pintos repository: https://web.stanford.edu/class/cs140/projects/pintos/


## 개발 환경 권장 사항
- OS: Ubuntu 24.04 LTS 혹은 WSL2 기반 Ubuntu 
- 컴파일러: GCC 10 이상 (실습 검증은 GCC 11.4 기준)
- 필수 도구: GCC, GNU binutils, GNU make, Perl, QEMU (`qemu-system-x86`), Git

패키지 설치 예시는 다음과 같다.
```bash
sudo apt update
sudo apt install build-essential binutils qemu-system-x86 perl git
```

## 프로젝트 구조
- `threads/`: 스레드 서브시스템과 빌드 스크립트, 실습 시 수정 대상
- `tests/threads/`: 자동 채점을 위한 스레드 테스트 (변경 금지)
- `utils/`: `pintos`, `pintos-gdb` 등 실행 및 디버깅 유틸리티
- `devices/`: 타이머 등 장치 에뮬레이션 계층
- `lib/`: 커널/유저 공용 라이브러리 및 자료구조
필요한 자료구조는 `lib/kernel` 하위 구현을 활용한다.

## 프로젝트 수행 순서
먼저 `threads` 디렉터리에 포함된 부분 구현 스레드 시스템을 살펴 Thread Fork, 기본 Round-Robin 스케줄러, 세마포어 등 이미 제공되는 구성 요소를 이해한다. 구현 목표는 해당 기반 위에서 추가 스케줄링 기법을 완성하는 것이다. 준비 큐에서 스레드가 어떤 순서로 선택되어도 동작이 일관되어야 하며, 인터럽트 허용 구간 어디에 `thread_yield()`를 삽입하더라도 올바르게 처리되도록 동기화를 신경 쓴다. Pintos는 각 스레드에 4KB 미만의 커널 스택을 배정하므로 큰 지역 변수의 선언을 피한다. 스케줄링과 관련된 타이머와 타이머 인터럽트 동작은 `devices/timer.c`와 `timer.h`를 참고해 이해한다. 


## 빌드 및 실행 방법
```bash
git clone <repo-url>
cd kjnu0522001-pintos/threads
make
../utils/pintos -- run priority-preempt
```
전체 테스트는 `make check`로 실행할 수 있다. 

## 기본 스케줄러 동작 점검
`thread_tick()`은 `TIME_SLICE` 틱마다 `intr_yield_on_return()`을 호출해 스케줄러를 호출한다. tests/threads/priority-preempt-timer`는 이 동작이 유지되는지를 확인하는 테스트이다. 동일 우선순위를 가진 스레드는 생성 순서를 유지한 채 Round-Robin으로 실행되어야 한다. `tests/threads/priority-fifo`는 스케줄러 확장 이후에도 기본 Round-Robin 동작이 유지되는지 확인한다. 

## 프로젝트 요구 사항
1. ** 선점형 우선순위 스케줄링 ** 
우선순위가 높은 스레드가 항상 먼저 실행되도록 `ready_list`와 동기화 대기열을 우선순위 순으로 유지한다. 실행 중 스레드가 `thread_set_priority()`로 우선순위를 낮추면 즉시 더 높은 우선순위 스레드가 선점해야 하며, 대기 중이던 높은 우선순위 스레드가 생성되거나 Unblock 될 때에도 즉시 Context Switch가 일어나야 한다. 세마포어 및 조건 변수 대기열 역시 우선순위 순으로 정렬하며 Unblock마다 최고 우선순위 스레드를 선택한다.  

2. ** 에이징 적용 ** 
낮은 우선순위 스레드의 기아상태(Starvation)를 방지하기 위해 준비 큐에서 대기 중인 스레드의 `age` 값을 매 틱마다 1씩 증가시키고, `age`가 20에 도달하면 우선순위를 한 단계 상승시킨 뒤 `age`를 0으로 초기화한다. 이 과정을 반복해 스레드가 최대 기본 우선순위(`PRI_DEFAULT`)까지 회복하고 필요한 시점에 다른 스레드를 선점하도록 한다. 

3. ** MLFQS(Simplified Multi-Level Feedback Queue Scheduler) ** 
`-mlfqs` 플래그 사용 시 세 단계(Q0, Q1, Q2)의 피드백 큐를 사용하는 단순한 형태의 MLFQ. 모든 스레드는 Q0에서 시작하고, 각 큐의 타임 슬라이스는 Q0=2틱, Q1=4틱, Q2=8틱이다. 주어진 시간 슬라이스를 모두 소모하면 Q0→Q1→Q2 순으로 강등되고, 대기 중에는 에이징 정책과 동일하게 매 틱 `age`를 1씩 증가시켜 `age`가 20이 되면 한 단계 상위 큐로 승급시키며 `age`를 0으로 초기화한다. 이때 상위 큐가 비어 있어야만 다음 큐의 스레드를 실행하며, 큐의 승급과 강등은 타이머 틱 단위로 수행한다. 
