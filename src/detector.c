#include "network.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "demo.h"
#include "option_list.h"
//add by kali*****************
#include <unistd.h>
#include <dirent.h>
#include <string.h>
//add by kali*****************
#ifdef OPENCV
#include "opencv2/highgui/highgui_c.h"
#endif

static int coco_ids[] = {1,2,3,4,5,6,7,8,9,10,11,13,14,15,16,17,18,19,20,21,22,23,24,25,27,28,31,32,33,34,35,36,37,38,39,40,41,42,43,44,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,67,70,72,73,74,75,76,77,78,79,80,81,82,84,85,86,87,88,89,90};

void train_detector(char *datacfg, char *cfgfile, char *weightfile, int *gpus, int ngpus, int clear)
{
    list *options = read_data_cfg(datacfg);
    char *train_images = option_find_str(options, "train", "data/train.list");
    char *backup_directory = option_find_str(options, "backup", "/backup/");

    srand(time(0));
    char *base = basecfg(cfgfile);
    printf("%s\n", base);
    float avg_loss = -1;
    network *nets = calloc(ngpus, sizeof(network));

    srand(time(0));
    int seed = rand();
    int i;
    for(i = 0; i < ngpus; ++i){
        srand(seed);
#ifdef GPU
        cuda_set_device(gpus[i]);
#endif
        nets[i] = parse_network_cfg(cfgfile);
        if(weightfile){
            load_weights(&nets[i], weightfile);
        }
        if(clear) *nets[i].seen = 0;
        nets[i].learning_rate *= ngpus;
    }
    srand(time(0));
    network net = nets[0];

    int imgs = net.batch * net.subdivisions * ngpus;
    printf("Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    data train, buffer;

    layer l = net.layers[net.n - 1];

    int classes = l.classes;
    float jitter = l.jitter;

    list *plist = get_paths(train_images);
    //int N = plist->size;
    char **paths = (char **)list_to_array(plist);

    load_args args = {0};
    args.w = net.w;
    args.h = net.h;
    args.paths = paths;
    args.n = imgs;
    args.m = plist->size;
    args.classes = classes;
    args.jitter = jitter;
    args.num_boxes = l.max_boxes;
    args.d = &buffer;
    args.type = DETECTION_DATA;
    args.threads = 8;

    args.angle = net.angle;
    args.exposure = net.exposure;
    args.saturation = net.saturation;
    args.hue = net.hue;

    pthread_t load_thread = load_data(args);
    clock_t time;
    int count = 0;
    //while(i*imgs < N*120){
    while(get_current_batch(net) < net.max_batches){
        if(l.random && count++%10 == 0){
            printf("Resizing\n");
            int dim = (rand() % 10 + 10) * 32;
            if (get_current_batch(net)+200 > net.max_batches) dim = 608;
            //int dim = (rand() % 4 + 16) * 32;
            printf("%d\n", dim);
            args.w = dim;
            args.h = dim;

            pthread_join(load_thread, 0);
            train = buffer;
            free_data(train);
            load_thread = load_data(args);

            for(i = 0; i < ngpus; ++i){
                resize_network(nets + i, dim, dim);
            }
            net = nets[0];
        }
        time=clock();
        pthread_join(load_thread, 0);
        train = buffer;
        load_thread = load_data(args);

        /*
           int k;
           for(k = 0; k < l.max_boxes; ++k){
           box b = float_to_box(train.y.vals[10] + 1 + k*5);
           if(!b.x) break;
           printf("loaded: %f %f %f %f\n", b.x, b.y, b.w, b.h);
           }
           image im = float_to_image(448, 448, 3, train.X.vals[10]);
           int k;
           for(k = 0; k < l.max_boxes; ++k){
           box b = float_to_box(train.y.vals[10] + 1 + k*5);
           printf("%d %d %d %d\n", truth.x, truth.y, truth.w, truth.h);
           draw_bbox(im, b, 8, 1,0,0);
           }
           save_image(im, "truth11");
         */

        printf("Loaded: %lf seconds\n", sec(clock()-time));

        time=clock();
        float loss = 0;
#ifdef GPU
        if(ngpus == 1){
            loss = train_network(net, train);
        } else {
            loss = train_networks(nets, ngpus, train, 4);
        }
#else
        loss = train_network(net, train);
#endif
        if (avg_loss < 0) avg_loss = loss;
        avg_loss = avg_loss*.9 + loss*.1;

        i = get_current_batch(net);
        printf("%d: %f, %f avg, %f rate, %lf seconds, %d images\n", get_current_batch(net), loss, avg_loss, get_current_rate(net), sec(clock()-time), i*imgs);
        if(i%1000==0 || (i < 1000 && i%100 == 0)){
#ifdef GPU
            if(ngpus != 1) sync_nets(nets, ngpus, 0);
#endif
            char buff[256];
            sprintf(buff, "%s/%s_%d.weights", backup_directory, base, i);
            save_weights(net, buff);
        }
        free_data(train);
    }
#ifdef GPU
    if(ngpus != 1) sync_nets(nets, ngpus, 0);
#endif
    char buff[256];
    sprintf(buff, "%s/%s_final.weights", backup_directory, base);
    save_weights(net, buff);
}


static int get_coco_image_id(char *filename)
{
    char *p = strrchr(filename, '_');
    return atoi(p+1);
}

static void print_cocos(FILE *fp, char *image_path, box *boxes, float **probs, int num_boxes, int classes, int w, int h)
{
    int i, j;
    int image_id = get_coco_image_id(image_path);
    for(i = 0; i < num_boxes; ++i){
        float xmin = boxes[i].x - boxes[i].w/2.;
        float xmax = boxes[i].x + boxes[i].w/2.;
        float ymin = boxes[i].y - boxes[i].h/2.;
        float ymax = boxes[i].y + boxes[i].h/2.;

        if (xmin < 0) xmin = 0;
        if (ymin < 0) ymin = 0;
        if (xmax > w) xmax = w;
        if (ymax > h) ymax = h;

        float bx = xmin;
        float by = ymin;
        float bw = xmax - xmin;
        float bh = ymax - ymin;

        for(j = 0; j < classes; ++j){
            if (probs[i][j]) fprintf(fp, "{\"image_id\":%d, \"category_id\":%d, \"bbox\":[%f, %f, %f, %f], \"score\":%f},\n", image_id, coco_ids[j], bx, by, bw, bh, probs[i][j]);
        }
    }
}

void print_detector_detections(FILE **fps, char *id, box *boxes, float **probs, int total, int classes, int w, int h)
{
    int i, j;
    for(i = 0; i < total; ++i){
        float xmin = boxes[i].x - boxes[i].w/2.;
        float xmax = boxes[i].x + boxes[i].w/2.;
        float ymin = boxes[i].y - boxes[i].h/2.;
        float ymax = boxes[i].y + boxes[i].h/2.;

        if (xmin < 0) xmin = 0;
        if (ymin < 0) ymin = 0;
        if (xmax > w) xmax = w;
        if (ymax > h) ymax = h;

        for(j = 0; j < classes; ++j){
            if (probs[i][j]) fprintf(fps[j], "%s %f %f %f %f %f\n", id, probs[i][j],
                    xmin, ymin, xmax, ymax);
        }
    }
}

void print_imagenet_detections(FILE *fp, int id, box *boxes, float **probs, int total, int classes, int w, int h)
{
    int i, j;
    for(i = 0; i < total; ++i){
        float xmin = boxes[i].x - boxes[i].w/2.;
        float xmax = boxes[i].x + boxes[i].w/2.;
        float ymin = boxes[i].y - boxes[i].h/2.;
        float ymax = boxes[i].y + boxes[i].h/2.;

        if (xmin < 0) xmin = 0;
        if (ymin < 0) ymin = 0;
        if (xmax > w) xmax = w;
        if (ymax > h) ymax = h;

        for(j = 0; j < classes; ++j){
            int class = j;
            if (probs[i][class]) fprintf(fp, "%d %d %f %f %f %f %f\n", id, j+1, probs[i][class],
                    xmin, ymin, xmax, ymax);
        }
    }
}

void validate_detector(char *datacfg, char *cfgfile, char *weightfile, char *outfile)
{
    int j;
    list *options = read_data_cfg(datacfg);
    char *valid_images = option_find_str(options, "valid", "data/train.list");
    char *name_list = option_find_str(options, "names", "data/names.list");
    char *prefix = option_find_str(options, "results", "results");
    char **names = get_labels(name_list);
    char *mapf = option_find_str(options, "map", 0);
    int *map = 0;
    if (mapf) map = read_map(mapf);

    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    fprintf(stderr, "Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    srand(time(0));

    list *plist = get_paths(valid_images);
    char **paths = (char **)list_to_array(plist);

    layer l = net.layers[net.n-1];
    int classes = l.classes;

    char buff[1024];
    char *type = option_find_str(options, "eval", "voc");
    FILE *fp = 0;
    FILE **fps = 0;
    int coco = 0;
    int imagenet = 0;
    if(0==strcmp(type, "coco")){
        if(!outfile) outfile = "coco_results";
        snprintf(buff, 1024, "%s/%s.json", prefix, outfile);
        fp = fopen(buff, "w");
        fprintf(fp, "[\n");
        coco = 1;
    } else if(0==strcmp(type, "imagenet")){
        if(!outfile) outfile = "imagenet-detection";
        snprintf(buff, 1024, "%s/%s.txt", prefix, outfile);
        fp = fopen(buff, "w");
        imagenet = 1;
        classes = 200;
    } else {
        if(!outfile) outfile = "comp4_det_test_";
        fps = calloc(classes, sizeof(FILE *));
        for(j = 0; j < classes; ++j){
            snprintf(buff, 1024, "%s/%s%s.txt", prefix, outfile, names[j]);
            fps[j] = fopen(buff, "w");
        }
    }


    box *boxes = calloc(l.w*l.h*l.n, sizeof(box));
    float **probs = calloc(l.w*l.h*l.n, sizeof(float *));
    for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = calloc(classes, sizeof(float *));

    int m = plist->size;
    int i=0;
    int t;

    float thresh = .005;
    float nms = .45;

    int nthreads = 4;
    image *val = calloc(nthreads, sizeof(image));
    image *val_resized = calloc(nthreads, sizeof(image));
    image *buf = calloc(nthreads, sizeof(image));
    image *buf_resized = calloc(nthreads, sizeof(image));
    pthread_t *thr = calloc(nthreads, sizeof(pthread_t));

    load_args args = {0};
    args.w = net.w;
    args.h = net.h;
    args.type = IMAGE_DATA;

    for(t = 0; t < nthreads; ++t){
        args.path = paths[i+t];
        args.im = &buf[t];
        args.resized = &buf_resized[t];
        thr[t] = load_data_in_thread(args);
    }
    time_t start = time(0);
    for(i = nthreads; i < m+nthreads; i += nthreads){
        fprintf(stderr, "%d\n", i);
        for(t = 0; t < nthreads && i+t-nthreads < m; ++t){
            pthread_join(thr[t], 0);
            val[t] = buf[t];
            val_resized[t] = buf_resized[t];
        }
        for(t = 0; t < nthreads && i+t < m; ++t){
            args.path = paths[i+t];
            args.im = &buf[t];
            args.resized = &buf_resized[t];
            thr[t] = load_data_in_thread(args);
        }
        for(t = 0; t < nthreads && i+t-nthreads < m; ++t){
            char *path = paths[i+t-nthreads];
            char *id = basecfg(path);
            float *X = val_resized[t].data;
            network_predict(net, X);
            int w = val[t].w;
            int h = val[t].h;
            get_region_boxes(l, w, h, thresh, probs, boxes, 0, map, .5);
            if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, classes, nms);
            if (coco){
                print_cocos(fp, path, boxes, probs, l.w*l.h*l.n, classes, w, h);
            } else if (imagenet){
                print_imagenet_detections(fp, i+t-nthreads+1, boxes, probs, l.w*l.h*l.n, classes, w, h);
            } else {
                print_detector_detections(fps, id, boxes, probs, l.w*l.h*l.n, classes, w, h);
            }
            free(id);
            free_image(val[t]);
            free_image(val_resized[t]);
        }
    }
    for(j = 0; j < classes; ++j){
        if(fps) fclose(fps[j]);
    }
    if(coco){
        fseek(fp, -2, SEEK_CUR); 
        fprintf(fp, "\n]\n");
        fclose(fp);
    }
    fprintf(stderr, "Total Detection Time: %f Seconds\n", (double)(time(0) - start));
}

void validate_detector_recall(char *datacfg, char *cfgfile, char *weightfile, char *record_path)//kali-add the first parameter
{
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    fprintf(stderr, "Learning Rate: %g, Momentum: %g, Decay: %g\n", net.learning_rate, net.momentum, net.decay);
    srand(time(0));

    //change by kali
    list *options = read_data_cfg(datacfg);
    char *valid_images_path = option_find_str(options, "valid", "data/train.list");
    list *plist = get_paths(valid_images_path);
    char **paths = (char **)list_to_array(plist);
    //change by kali

    layer l = net.layers[net.n-1];
    int classes = l.classes;

    int j, k;
    box *boxes = calloc(l.w*l.h*l.n, sizeof(box));
    float **probs = calloc(l.w*l.h*l.n, sizeof(float *));
    for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = calloc(classes, sizeof(float *));

    int m = plist->size;
    int i=0;

    //float thresh = .001;
    float thresh = .25;
    float iou_thresh = .5;
    float nms = .4;

    int total = 0;
    int correct = 0;
    int proposals = 0;
    float avg_iou = 0;

    if(!record_path)
        record_path = "record.txt";

    FILE *fp;
    fp=fopen(record_path,"w");
    if(fp)
    {
        for(i = 0; i < m; ++i)
        {
            char *path = paths[i];
            image orig = load_image_color(path, 0, 0);
            image sized = resize_image(orig, net.w, net.h);
            char *id = basecfg(path);
            network_predict(net, sized.data);
            get_region_boxes(l, 1, 1, thresh, probs, boxes, 1, 0, .5);
            if (nms) do_nms(boxes, probs, l.w*l.h*l.n, 1, nms);

            char labelpath[4096];
            find_replace(path, "images", "labels", labelpath);
            find_replace(labelpath, "JPEGImages", "labels", labelpath);
            find_replace(labelpath, ".jpg", ".txt", labelpath);
            find_replace(labelpath, ".JPEG", ".txt", labelpath);

            int num_labels = 0;
            box_label *truth = read_boxes(labelpath, &num_labels);
            for(k = 0; k < l.w*l.h*l.n; ++k){
                if(probs[k][0] > thresh){
                    ++proposals;
                }
            }
            for (j = 0; j < num_labels; ++j) {
                ++total;
                box t = {truth[j].x, truth[j].y, truth[j].w, truth[j].h};
                float best_iou = 0;
                for(k = 0; k < l.w*l.h*l.n; ++k){
                    float iou = box_iou(boxes[k], t);
                    if(probs[k][0] > thresh && iou > best_iou){
                        best_iou = iou;
                    }
                }
                avg_iou += best_iou;
                if(best_iou > iou_thresh){
                    ++correct;
                }
            }

            //fprintf(stderr, "%5d %5d %5d\tRPs/Img: %.2f\tIOU: %.2f%%\tRecall:%.2f%%\n", i, correct, total, (float)proposals/(i+1), avg_iou*100/total, 100.*correct/total);
            fprintf(stderr, "ID:%5d Correct:%5d Total:%5d\tRPs/Img: %.2f\tIOU: %.2f%%\tRecall:%.2f%%\t", i, correct, total, (float)proposals/(i+1), avg_iou*100/total, 100.*correct/total);
            fprintf(stderr, "proposals:%5d\tPrecision:%.2f%%\n",proposals,100.*correct/(float)proposals);
            fprintf(fp,"ID:%5d Correct:%5d Total:%5d\tRPs/Img: %.2f\tIOU: %.2f%%\tRecall:%.2f%%\t", i, correct, total, (float)proposals/(i+1), avg_iou*100/total, 100.*correct/total);
            fprintf(fp, "proposals:%5d\tPrecision:%.2f%%\n",proposals,100.*correct/(float)proposals);
            free(id);
            free_image(orig);
            free_image(sized);
        }
        fclose(fp);
    }
}

void test_detector(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh, float hier_thresh)
{
    list *options = read_data_cfg(datacfg);
    char *name_list = option_find_str(options, "names", "data/names.list");
    char **names = get_labels(name_list);

    image **alphabet = load_alphabet();
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    srand(2222222);
    clock_t time;
    char buff[1024];
    char *input = buff;
    int j;
    float nms=.4;
    while(1){
        if(filename){
            strncpy(input, filename, 1024);
        } else {
            printf("Enter Image Path: ");
            fflush(stdout);
            input = fgets(input, 1024, stdin);
            if(!input) return;
            strtok(input, "\n");
        }
        image im = load_image_color(input,0,0);
        image sized = resize_image(im, net.w, net.h);
        layer l = net.layers[net.n-1];

        box *boxes = calloc(l.w*l.h*l.n, sizeof(box));
        float **probs = calloc(l.w*l.h*l.n, sizeof(float *));
        for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = calloc(l.classes + 1, sizeof(float *));

        float *X = sized.data;
        time=clock();
        network_predict(net, X);
        printf("%s: Predicted in %f seconds.\n", input, sec(clock()-time));
        get_region_boxes(l, 1, 1, thresh, probs, boxes, 0, 0, hier_thresh);
        if (l.softmax_tree && nms) do_nms_obj(boxes, probs, l.w*l.h*l.n, l.classes, nms);
        else if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, l.classes, nms);
        printf("%d\n",l.classes);
        draw_detections(im, l.w*l.h*l.n, thresh, boxes, probs, names, alphabet, l.classes);
        save_image(im, "predictions");
        show_image(im, "predictions");

        free_image(im);
        free_image(sized);
        free(boxes);
        free_ptrs((void **)probs, l.w*l.h*l.n);
#ifdef OPENCV
        cvWaitKey(0);
        cvDestroyAllWindows();
#endif
        if (filename) break;
    }
}

//add by kali*****************
void listDir(char *read_filename, char *save_filename, network net,float nms,char **names,image **alphabet,float thresh, float hier_thresh)
{
    DIR *pDir;
    struct dirent *ent;
    char new_read_path[1024];
    char new_save_path[1024];

    pDir=opendir(read_filename);

    while((ent=readdir(pDir))!=NULL)
    {
        if(strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0)//文件夹
            continue;
        if(ent->d_type == 4 )//目录
        {
            printf("direction:%s\n",read_filename);
            memset(new_read_path,0,sizeof(new_read_path));
            memset(new_save_path,0,sizeof(new_save_path));

            sprintf(new_read_path,"%s/%s",read_filename,ent->d_name);
            sprintf(new_save_path,"%s/%s",save_filename,ent->d_name);
            printf("new read direction:%s\n",new_read_path);
            printf("new save direction:%s\n",new_save_path);
            //判断新的存储路径是否存在，不存在创建
            if(access(new_save_path,R_OK|W_OK) !=0 )//文件夹不存在,创建
            {
                char mkdir_cmd[1024];
                memset(mkdir_cmd,0,sizeof(mkdir_cmd));
                sprintf(mkdir_cmd,"mkdir %s",new_save_path);
                system(mkdir_cmd);
            }
            listDir(new_read_path,new_save_path,net,nms,names,alphabet,thresh,hier_thresh);
        }
        else if(ent->d_type == 8)//文件
        {
            //检测处理
            char image_path[1024];
            memset(image_path,0,sizeof(image_path));
            sprintf(image_path,"%s/%s",read_filename,ent->d_name);
            printf("file:%s\n",image_path);
            image im = load_image_color(image_path,0,0);
            image sized = resize_image(im, net.w, net.h);
            layer l = net.layers[net.n-1];

            box *boxes = calloc(l.w*l.h*l.n, sizeof(box));
            float **probs = calloc(l.w*l.h*l.n, sizeof(float *));
            for(int j = 0; j < l.w*l.h*l.n; ++j) probs[j] = calloc(l.classes + 1, sizeof(float *));

            float *X = sized.data;
            network_predict(net, X);
            get_region_boxes(l, 1, 1, thresh, probs, boxes, 0, 0, hier_thresh);
            if (l.softmax_tree && nms) do_nms_obj(boxes, probs, l.w*l.h*l.n, l.classes, nms);
            else if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, l.classes, nms);

            //保存图片
            draw_detections(im, l.w*l.h*l.n, thresh, boxes, probs, names, alphabet, l.classes);
            char *pos = strrchr(ent->d_name,'.');
            int len = (pos-ent->d_name);
            char sub_image_name[1024];
            memset(sub_image_name,0,sizeof(sub_image_name));
            strncpy(sub_image_name,ent->d_name,len*sizeof(char));
            char save_path[1024];
            memset(save_path,0,sizeof(save_path));
            sprintf(save_path,"%s/%s",save_filename,sub_image_name);
            printf("save path:%s\n",save_path);
            save_image(im, save_path);

            free_image(im);
            free_image(sized);

            //保存txt文件
            char txt_save_path[1024];
            memset(txt_save_path,0,sizeof(txt_save_path));
            sprintf(txt_save_path,"%s/%s.txt",save_filename,sub_image_name);
            FILE *fp;
            fp=fopen(txt_save_path,"w");
            if(fp)
            {
                fprintf(fp,"%s %s %s %s %s %s\n","label", "x", "y", "w", "h" ,"prob");
                int box_num = l.w*l.h*l.n;
                for(int i=0;i<box_num;i++)
                {
                    int label_index = max_index(probs[i], l.classes);
                    char *label = names[label_index];
                    float prob = probs[i][label_index];
                    if(prob > thresh)
                        fprintf(fp,"%s %f %f %f %f %s\n",label, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, prob);
                }
                fclose(fp);

            }
            free(boxes);
            free_ptrs((void **)probs, l.w*l.h*l.n);
            //检测处理
        }
    }
}
//add by kali*****************

//add by kali*****************
void test_detector_kali(char *datacfg, char *cfgfile, char *weightfile, char *read_filename, char *save_filename, float thresh, float hier_thresh)
{
    if(!save_filename)
    {
        printf("Error:%s not exist!\n",save_filename);
        return;
    }
    list *options = read_data_cfg(datacfg);
    char *name_list = option_find_str(options, "names", "data/names.list");
    char **names = get_labels(name_list);

    image **alphabet = load_alphabet();
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    float nms=.4;
    listDir(read_filename,save_filename,net,nms,names,alphabet,thresh,hier_thresh);//循环检测图片,批量处理
}
//add by kali*****************

//add by kali*****************
void test_detector_kali_txt(char *datacfg, char *cfgfile, char *weightfile, char *read_filename, char *save_filename, char *txt_path, float thresh, float hier_thresh)
{
    if(!save_filename)
    {
        printf("Error:%s not exist!\n",save_filename);
        return;
    }
    list *options = read_data_cfg(datacfg);
    char *name_list = option_find_str(options, "names", "data/names.list");
    char **names = get_labels(name_list);

    image **alphabet = load_alphabet();
    network net = parse_network_cfg(cfgfile);
    if(weightfile){
        load_weights(&net, weightfile);
    }
    set_batch_network(&net, 1);
    float nms=.4;

    //直接读取txt文件中图片的路径
    FILE *read_fp;
    read_fp=fopen(txt_path,"r");
    if(read_fp)
    {
        char* line_name = calloc(128,sizeof(line_name));
        memset(line_name,0,128);
        line_name = fgetl(read_fp);
        while(line_name)
        {
            printf("kali-4\n");
            //检测处理
            char image_path[1024];
            memset(image_path,0,sizeof(image_path));
            sprintf(image_path,"%s/%s.jpg",read_filename,line_name);
            printf("file:%s\n",image_path);
            image im = load_image_color(image_path,0,0);
            image sized = resize_image(im, net.w, net.h);
            layer l = net.layers[net.n-1];

            box *boxes = calloc(l.w*l.h*l.n, sizeof(box));
            float **probs = calloc(l.w*l.h*l.n, sizeof(float *));
            for(int j = 0; j < l.w*l.h*l.n; ++j) probs[j] = calloc(l.classes + 1, sizeof(float *));

            float *X = sized.data;
            network_predict(net, X);
            get_region_boxes(l, 1, 1, thresh, probs, boxes, 0, 0, hier_thresh);
            if (l.softmax_tree && nms) do_nms_obj(boxes, probs, l.w*l.h*l.n, l.classes, nms);
            else if (nms) do_nms_sort(boxes, probs, l.w*l.h*l.n, l.classes, nms);
            //检测处理

            //判断新的存储路径是否存在，不存在创建
            char *in_pos = strrchr(line_name,'/');
	    if(in_pos == NULL)
                in_pos = line_name;
            int len = (in_pos-line_name);
            char in_name[64];
            memset(in_name,0,sizeof(in_name));
            strncpy(in_name,line_name,len*sizeof(char));
            char new_save_path[1024];
            memset(new_save_path,0,sizeof(new_save_path));
            sprintf(new_save_path,"%s/%s",save_filename,in_name);
            printf("%s\n",line_name);
            printf("%s\n",in_name);
            printf("%s\n",new_save_path);
            if(access(new_save_path,R_OK|W_OK) !=0 )//文件夹不存在,创建
            {
                printf("%s\n",new_save_path);
                char mkdir_cmd[1024];
                memset(mkdir_cmd,0,sizeof(mkdir_cmd));
                sprintf(mkdir_cmd,"mkdir %s",new_save_path);
                system(mkdir_cmd);
            }

            //保存图片
            draw_detections(im, l.w*l.h*l.n, thresh, boxes, probs, names, alphabet, l.classes);
            char save_path[1024];
            memset(save_path,0,sizeof(save_path));
            sprintf(save_path,"%s/%s",save_filename,line_name);
            printf("save path:%s\n",save_path);
            save_image(im, save_path);

            free_image(im);
            free_image(sized);
            //保存图片

            //保存txt文件
            char txt_save_path[1024];
            memset(txt_save_path,0,sizeof(txt_save_path));
            sprintf(txt_save_path,"%s/%s.txt",save_filename,line_name);
            FILE *fp;
            fp=fopen(txt_save_path,"w");
            if(fp)
            {
                fprintf(fp,"%s %s %s %s %s %s\n","label", "x", "y", "w", "h" ,"prob");
                int box_num = l.w*l.h*l.n;
                for(int i=0;i<box_num;i++)
                {
                    int label_index = max_index(probs[i], l.classes);
                    char *label = names[label_index];
                    float prob = probs[i][label_index];
                    if(prob > thresh)
                    {
                        fprintf(fp,"%s %f %f %f %f %f\n",label, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, prob);
                    }
                }
                fclose(fp);
            }
            free(boxes);
            free_ptrs((void **)probs, l.w*l.h*l.n);
            //保存txt文件
            memset(line_name,0,sizeof(line_name));
            line_name = fgetl(read_fp);
        }
        free(line_name);
        fclose(read_fp);
    }
    //直接读取txt文件中图片的路径
}
//add by kali*****************

void run_detector(int argc, char **argv)
{
    char *prefix = find_char_arg(argc, argv, "-prefix", 0);
    float thresh = find_float_arg(argc, argv, "-thresh", .24);
    float hier_thresh = find_float_arg(argc, argv, "-hier", .5);
    int cam_index = find_int_arg(argc, argv, "-c", 0);
    int frame_skip = find_int_arg(argc, argv, "-s", 0);
    if(argc < 4){
        fprintf(stderr, "usage: %s %s [train/test/valid] [cfg] [weights (optional)]\n", argv[0], argv[1]);
        return;
    }
    char *gpu_list = find_char_arg(argc, argv, "-gpus", 0);
    char *outfile = find_char_arg(argc, argv, "-out", 0);
    char *record_path = find_char_arg(argc, argv, "-record", 0);
    int *gpus = 0;
    int gpu = 0;
    int ngpus = 0;
    if(gpu_list){
        printf("%s\n", gpu_list);
        int len = strlen(gpu_list);
        ngpus = 1;
        int i;
        for(i = 0; i < len; ++i){
            if (gpu_list[i] == ',') ++ngpus;
        }
        gpus = calloc(ngpus, sizeof(int));
        for(i = 0; i < ngpus; ++i){
            gpus[i] = atoi(gpu_list);
            gpu_list = strchr(gpu_list, ',')+1;
        }
    } else {
        gpu = gpu_index;
        gpus = &gpu;
        ngpus = 1;
    }

    int clear = find_arg(argc, argv, "-clear");

    char *datacfg = argv[3];
    char *cfg = argv[4];
    char *weights = (argc > 5) ? argv[5] : 0;
    char *filename = (argc > 6) ? argv[6]: 0;
    if(0==strcmp(argv[2], "test")) test_detector(datacfg, cfg, weights, filename, thresh, hier_thresh);
    else if(0==strcmp(argv[2], "train")) train_detector(datacfg, cfg, weights, gpus, ngpus, clear);
    else if(0==strcmp(argv[2], "valid")) validate_detector(datacfg, cfg, weights, outfile);
    else if(0==strcmp(argv[2], "recall")) validate_detector_recall(datacfg,cfg, weights,record_path);
    else if(0==strcmp(argv[2], "demo")) {
        list *options = read_data_cfg(datacfg);
        int classes = option_find_int(options, "classes", 20);
        char *name_list = option_find_str(options, "names", "data/names.list");
        char **names = get_labels(name_list);
        demo(cfg, weights, thresh, cam_index, filename, names, classes, frame_skip, prefix, hier_thresh);
    }
}
