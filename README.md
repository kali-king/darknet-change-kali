![Darknet Logo](http://pjreddie.com/media/files/darknet-black-small.png)

#Darknet#
Darknet is an open source neural network framework written in C and CUDA. It is fast, easy to install, and supports CPU and GPU computation.

For more information see the [Darknet project website](http://pjreddie.com/darknet).

For questions or issues please use the [Google Group](https://groups.google.com/forum/#!forum/darknet).


############################

batch-testing for yolov2

usage:example
(exe-name) (function) (net) (model) (direction of images) (store direction for detection result including png and txt)
./darknet batch_detect cfg/yolo.cfg weight/yolo.weights example/TVHI example/result_TVHI

note:
1.just add a function for batch test.

2.example/TVHI is the direction of images.
for example:
example/TVHI/handshake_0001
example/TVHI/handshake_0002
"handshake_0001" and "handshake_0002" are two file in file named "111"
there are some pictures in file "handshake_0001" and "handshake_0002"

3.example/result_TVHI is the direction to store the detection result including .png and .txt.
for example:
example/result_TVHI/handshake_0001
example/result_TVHI/handshake_0002
you must create the direction "example/result_TVHI" in advance!
it will create the file "handshake_0001" and "handshake_0001" in file "111-result" corresponding to note2.
and the .png and .txt will be stored in the file "handshake_0001" and "handshake_0001" corresponding to note2.

