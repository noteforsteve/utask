utask
========

utask (micro-task)

uTask is a small "C" based single header and single source file task based embedded operating system. uTask uses a message queuing model along with a traditional message loop to dispatch generic messages to user defined tasks. Tasks at their core are function pointers which receive messages along with an id and an optional memory block pointer. uTask allows the programmer to dispatch messages which should execute immediately or after specific amount of relative time has elapsed. uTask is intended to be used as a replacement for the common embedded fore-ground back-ground construct that usually gets developed in small embedded systems. It is not a replacement for a time-sliced or priority based scheduler / operating system. uTask goals are to be small and portable it can be grown into a larger system.


