
#Darknet#
Darknet is an open source neural network framework written in C and CUDA. It is fast, easy to install, and supports CPU and GPU computation.

For more information see the [Darknet project website](http://pjreddie.com/darknet).

For questions or issues please use the [Google Group](https://groups.google.com/forum/#!forum/darknet).


############################

batch-testing for yolov2

##################################
function1:give the direction

usage:example
(exe-name) (function) (data) (net) (model) (direction of images) (store direction for detection result including png and txt)
./darknet batch_detect cfg/voc.data cfg/yolo.cfg weight/yolo.weights example/TVHI example/result_TVHI

note:

paramter:.example/TVHI is the direction of images.
for example:
example/TVHI/handshake_0001
example/TVHI/handshake_0002
"handshake_0001" and "handshake_0002" are two file in file named "111"
there are some pictures in file "handshake_0001" and "handshake_0002"

paramter:.example/result_TVHI is the direction to store the detection result including .png and .txt.
for example:
example/result_TVHI/handshake_0001
example/result_TVHI/handshake_0002
you must create the direction "example/result_TVHI" in advance!
it will create the file "handshake_0001" and "handshake_0001" in file "111-result" corresponding to note2.
and the .png and .txt will be stored in the file "handshake_0001" and "handshake_0001" corresponding to note2.
.txt including the label, position of detection box and confidence


###################################
function2:give the txt store the image direction based on function1

usage:example
(exe-name) (function) (data) (net) (model) (direction of images) (store direction for detection result including png and txt) (the sub path of images)
./darknet batch_detect cfg/voc.data cfg/yolo.cfg weight/yolo.weights example/TVHI example/result_TVHI example/name.txt



###################################
function3:add a parameter for function "validate_detector_recall" in detector.c
parameter:-record
for example -record ./record.txt
role:store the process of valid　recall

###################################
function4: do some change in function "validate_detector_recall" in detector.c
change:list *plist = get_paths(valid_images_path);
note:we will get the valid data path from the dataflg rather than a defaut direction


