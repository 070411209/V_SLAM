#include "feature_tracker.h"

int FeatureTracker::n_id = 0;

bool inBorder(const cv::Point2f &pt)
{
    const int BORDER_SIZE = 1;
    int img_x = cvRound(pt.x);
    int img_y = cvRound(pt.y);
    return BORDER_SIZE <= img_x && img_x < COL - BORDER_SIZE && BORDER_SIZE <= img_y && img_y < ROW - BORDER_SIZE;
}

void reduceVector(vector<cv::Point2f> &v, vector<uchar> status)
{
    int j = 0;
    for (int i = 0; i < int(v.size()); i++)
        if (status[i])
            v[j++] = v[i];
    v.resize(j);
}

void reduceVector(vector<int> &v, vector<uchar> status)
{
    int j = 0;
    for (int i = 0; i < int(v.size()); i++)
        if (status[i])
            v[j++] = v[i];
    v.resize(j);
}


FeatureTracker::FeatureTracker()
{
}

void FeatureTracker::setMask()
{
    if(FISHEYE)
        mask = fisheye_mask.clone();
    else
        mask = cv::Mat(ROW, COL, CV_8UC1, cv::Scalar(255));
    

    // prefer to keep features that are tracked for long time
    vector<pair<int, pair<cv::Point2f, int>>> cnt_pts_id;

    for (unsigned int i = 0; i < forw_pts.size(); i++)
        cnt_pts_id.push_back(make_pair(track_cnt[i], make_pair(forw_pts[i], ids[i])));

    sort(cnt_pts_id.begin(), cnt_pts_id.end(), [](const pair<int, pair<cv::Point2f, int>> &a, const pair<int, pair<cv::Point2f, int>> &b)
         {
            return a.first > b.first;
         });

    forw_pts.clear();
    ids.clear();
    track_cnt.clear();

    for (auto &it : cnt_pts_id)
    {
        if (mask.at<uchar>(it.second.first) == 255)
        {
            forw_pts.push_back(it.second.first);
            ids.push_back(it.second.second);
            track_cnt.push_back(it.first);
            cv::circle(mask, it.second.first, MIN_DIST, 0, -1);
        }
    }
}

void FeatureTracker::addPoints()
{
    for (auto &p : n_pts)
    {
        forw_pts.push_back(p);
        ids.push_back(-1);
        track_cnt.push_back(1);
    }
}


/*
    1��readImage������ͼ����������Լ������ٶȶ�����������洢��trackerData[i]�ı�����
    2��У�����������洢��cur_un_pts����֡����������ӵ��������� pre_un_pts����֡���ͱ�֡����µ�֮ǰ���������Ѿ����룩�У�
    �����ٶ� pts_velocity ��cur_pts��pre_pts��δ����У��������λ��
    3��cur_un_pts��pre_un_pts�����Ǽ򵥵�����λ�ã�����[(u-cx)/fx,[(v-cy)/fy];pts_velocityҲ���ǵ����������ٶȣ����ǣ������ٶ�/fx������ [(deltu/fx)/dt,(deltv/fy)/dt]
*/
void FeatureTracker::readImage(const cv::Mat &_img, double _cur_time)
{
    cv::Mat img;
    TicToc t_r;
    cur_time = _cur_time;

    /*�������EQUALIZEΪ1���ȶ�ͼ�����Ƚ����˵���*/
    if (EQUALIZE)  
    {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
        TicToc t_c;
        clahe->apply(_img, img);
        ROS_DEBUG("CLAHE costs: %fms", t_c.toc());
    }
    else
        img = _img;

    
    /*������״μ��㣬��ô�������������㺯��������  goodFeaturesToTrack */
    if (forw_img.empty())
    {
        prev_img = cur_img = forw_img = img;
    }
    else
    {
        forw_img = img;
    }

    forw_pts.clear();

    if (cur_pts.size() > 0)
    {
        TicToc t_o;
        vector<uchar> status;
        vector<float> err;

        /*������������ͼ��͵㶼��δ������У���ĵ�*/
        cv::calcOpticalFlowPyrLK(cur_img, forw_img, cur_pts, forw_pts, status, err, cv::Size(21, 21), 3);  // �¼�������������� forw_pts

        for (int i = 0; i < int(forw_pts.size()); i++)  //inBorder ��֤�¼����������Ч����forw_pts λ��ͼ����, ����Ч�ģ�����λ�������ڱ߽��ϵ�������ȥ��
            if (status[i] && !inBorder(forw_pts[i]))
                status[i] = 0;

        /*���˵�һ��ͨ��goodFeaturesToTrack��ýǵ��⣬
        ��������ͨ��calcOpticalFlowPyrLK��õģ�
        ����ÿ�ζ���status����ͬ�Ĳ��ֱ�������*/

        /*
            �������prev_pts��cur_pts��forw_pts��ֻ����ͬ�Ľǵ㱻���������ˣ����ǵ���Ŀ��Խ��Խ�٣���������������sizeһ������0
            Ϊ�˱�֤��Ч�Ľǵ���Ŀ������ں���ĳ����м���˵�ǰ��Ч�Ľǵ���forw_pts.size()�����С��MAX_CNT�Ļ����͵���goodFeaturesToTrack�������MAX_CNT - forw_pts.size()���ǵ㣬
            ��ӵ�forw_pts���棬��֤forw_pts����������һ�±�����MAX_CNT
            ���������и����⣬goodFeaturesToTrack�¼�������ĵ��п�����forw_pts�Ѿ������ˣ�Ҳ���ܲ�����
            ��Ȼ�����˲��䣬prev_pts��������Ŀ���cur_pts��forw_pts�٣�����ǰ�����������һһ��Ӧ�ģ�������status������
            ע�⣺����status�������ô��prev_pts  һֱ�������prev_pts��cur_pts, forw_pts������������������һֱ�ڱ�����

            cur_pts �������cur_pts,forw_pts��������������
            ������Ϊ����goodFeaturesToTrack����ӣ�
            ��˻��  prev_pts �����ࡣ
            �����forw_pts��û������µ㣬
            ��˴�ʱcur_pts �� forw_pts����һ���ĵ���
        */

        reduceVector(prev_pts, status);   //ǰǰ֡����һ�ε�ʱ��prev_pts�ǿյ�
        reduceVector(cur_pts, status);    //ǰ֡����һ�ε�ʱ��cur_pts�ھ���status����ǰ��ͨ��goodFeaturesToTrack���MAX_CNT���㣬
        // ����Ҳͨ��goodFeaturesToTrack�������µĽǵ㣬ʹ��һֱ����MAX_CNT��

        reduceVector(forw_pts, status);   //��ǰ֡��forw_pts �ھ���status����ǰ����ͨ�� calcOpticalFlowPyrLK ��õ�
        reduceVector(ids, status);        //ǰ֡��ids ��һ�ε�ʱ���ھ���status����ǰ������ ���� ǰ֡�ĸ���������ֵ����-1
        reduceVector(cur_un_pts, status); //ǰ֡��cur_un_pts�ھ���status����ǰ����cur_pts����У�����������
        reduceVector(track_cnt, status);  //ǰ֡��track_cnt��һ�ε�ʱ���ھ���status����ǰ����������ǰ֡�ĸ���������ֵ����1

        ROS_DEBUG("temporal optical flow costs: %fms", t_o.toc());
    }

    for (auto &n : track_cnt)  //vector<int> track_cnt������n++�ᵼ��track_cnt�����е�Ԫ�ص�����track_cnt��ӳ�˸����������ڼ���ͼ���г��ֹ�
        n++;

    if (PUB_THIS_FRAME)  // ����ζ�������Ƶ��ԭͼ��μ���cv�����ļ��㣬����û��ȥ����Ӧ������goodfeature���½ǵ�Ĺ���
    {
        rejectWithF();  //����ransac �޳�prev_pts��cur_pts��forw_pts��ids��cur_un_pts��track_cnt ��һЩ�����ŵĵ�

        ROS_DEBUG("set mask begins");
        TicToc t_m;
        setMask();      //���������mask��ȥ��mask��ɫ���ֵ�����
        ROS_DEBUG("set mask costs %fms", t_m.toc());

        ROS_DEBUG("detect feature begins");
        TicToc t_t;
        int n_max_cnt = MAX_CNT - static_cast<int>(forw_pts.size());

        /**
         * 
         * �����������������ͬ�Ľǵ�����MAX_CNT�󣬾����µ���goodFeaturesToTrack���½ǵ�
         * 
         * */

        if (n_max_cnt > 0)
        {
            if(mask.empty())
                cout << "mask is empty " << endl;
            if (mask.type() != CV_8UC1)
                cout << "mask type wrong " << endl;
            if (mask.size() != forw_img.size())
                cout << "wrong size " << endl;

            cv::goodFeaturesToTrack(forw_img, n_pts, MAX_CNT - forw_pts.size(), 0.01, MIN_DIST, mask);
            /*
                forw_img��ͼ��û��������У����ֱ��������������ٺ���������У�������뵽��һ������ cur_un_pts ��
                n_pts �Ƿ��صĽǵ�����
                MAX_CNT - forw_pts.size()�Ƿ��ص����ǵ���Ŀ��0.01�ǽǵ��Ʒ������
                MIN_DIST ͨ��yml��ȡ��Ŀǰ����Ϊ30����ѡ�ǵ㣬���������ΧMIN_DIST��Χ�ڳ��ֱ�����ǿ�Ľǵ㣬��ɾ���ýǵ㡣
                mask��ָ������Ȥ�������粻��Ҫ������ͼ��Ѱ�Ҹ���Ȥ�Ľǵ㣬������mask����ROI��������ԭ��Ҫ��mask��fisheye�Ͳ�������������
            */
        }
        else
            n_pts.clear();
        ROS_DEBUG("detect feature costs: %fms", t_t.toc());

        ROS_DEBUG("add feature begins");
        TicToc t_a;
        addPoints();

        /*
        ͨ�����������״μ����MAX_CNT���ǵ㣬Ȼ��addPoints();
        �� forw_pts ��ӽǵ����꣬
        ��ids��ӵ��������ǵ������ -1��
        �� track_cnt ��ӵ��ڽǵ������ 1
        ֻ�к��� updateID �Ż���� ids��ʹ����Ϊ-1��
        �� updateID ��feature_tracker_node.cpp �вŵ���
        */

        ROS_DEBUG("selectFeature costs: %fms", t_a.toc());
    }

    /*�ں��������cur_pts ����Ϊ��ǰ֡��prev_pts��prev_un_pts ����Ϊ��һ֡���״ε�ʱ��ΪNULL��*/
    prev_img = cur_img;
    prev_pts = cur_pts;
    prev_un_pts = cur_un_pts;
    cur_img = forw_img;
    cur_pts = forw_pts;

    undistortedPoints(); // ��cur_pts ������У����������� cur_un_pts

    prev_time = cur_time;
}

void FeatureTracker::rejectWithF()
{
    if (forw_pts.size() >= 8)
    {
        ROS_DEBUG("FM ransac begins");
        TicToc t_f;
        vector<cv::Point2f> un_cur_pts(cur_pts.size()), un_forw_pts(forw_pts.size());
        for (unsigned int i = 0; i < cur_pts.size(); i++)
        {
            Eigen::Vector3d tmp_p;
            m_camera->liftProjective(Eigen::Vector2d(cur_pts[i].x, cur_pts[i].y), tmp_p);
            tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + COL / 2.0;
            tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + ROW / 2.0;
            un_cur_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());

            m_camera->liftProjective(Eigen::Vector2d(forw_pts[i].x, forw_pts[i].y), tmp_p);
            tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + COL / 2.0;
            tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + ROW / 2.0;
            un_forw_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());
        }

        vector<uchar> status;
        cv::findFundamentalMat(un_cur_pts, un_forw_pts, cv::FM_RANSAC, F_THRESHOLD, 0.99, status);
        int size_a = cur_pts.size();
        reduceVector(prev_pts, status);
        reduceVector(cur_pts, status);
        reduceVector(forw_pts, status);
        reduceVector(cur_un_pts, status);
        reduceVector(ids, status);
        reduceVector(track_cnt, status);
        ROS_DEBUG("FM ransac: %d -> %lu: %f", size_a, forw_pts.size(), 1.0 * forw_pts.size() / size_a);
        ROS_DEBUG("FM ransac costs: %fms", t_f.toc());
    }
}

bool FeatureTracker::updateID(unsigned int i)
{
    if (i < ids.size())
    {
        if (ids[i] == -1)
            ids[i] = n_id++;
        return true;
    }
    else
        return false;
}

void FeatureTracker::readIntrinsicParameter(const string &calib_file)
{
    ROS_INFO("reading paramerter of camera %s", calib_file.c_str());
    m_camera = CameraFactory::instance()->generateCameraFromYamlFile(calib_file);
}

void FeatureTracker::showUndistortion(const string &name)
{
    cv::Mat undistortedImg(ROW + 600, COL + 600, CV_8UC1, cv::Scalar(0));
    vector<Eigen::Vector2d> distortedp, undistortedp;
    for (int i = 0; i < COL; i++)
        for (int j = 0; j < ROW; j++)
        {
            Eigen::Vector2d a(i, j);
            Eigen::Vector3d b;
            m_camera->liftProjective(a, b);
            distortedp.push_back(a);
            undistortedp.push_back(Eigen::Vector2d(b.x() / b.z(), b.y() / b.z()));
            //printf("%f,%f->%f,%f,%f\n)\n", a.x(), a.y(), b.x(), b.y(), b.z());
        }
    for (int i = 0; i < int(undistortedp.size()); i++)
    {
        cv::Mat pp(3, 1, CV_32FC1);
        pp.at<float>(0, 0) = undistortedp[i].x() * FOCAL_LENGTH + COL / 2;
        pp.at<float>(1, 0) = undistortedp[i].y() * FOCAL_LENGTH + ROW / 2;
        pp.at<float>(2, 0) = 1.0;
        //cout << trackerData[0].K << endl;
        //printf("%lf %lf\n", p.at<float>(1, 0), p.at<float>(0, 0));
        //printf("%lf %lf\n", pp.at<float>(1, 0), pp.at<float>(0, 0));
        if (pp.at<float>(1, 0) + 300 >= 0 && pp.at<float>(1, 0) + 300 < ROW + 600 && pp.at<float>(0, 0) + 300 >= 0 && pp.at<float>(0, 0) + 300 < COL + 600)
        {
            undistortedImg.at<uchar>(pp.at<float>(1, 0) + 300, pp.at<float>(0, 0) + 300) = cur_img.at<uchar>(distortedp[i].y(), distortedp[i].x());
        }
        else
        {
            //ROS_ERROR("(%f %f) -> (%f %f)", distortedp[i].y, distortedp[i].x, pp.at<float>(1, 0), pp.at<float>(0, 0));
        }
    }
    cv::imshow(name, undistortedImg);
    cv::waitKey(0);
}

void FeatureTracker::undistortedPoints()
{
    cur_un_pts.clear();
    cur_un_pts_map.clear();
    //cv::undistortPoints(cur_pts, un_pts, K, cv::Mat());
    for (unsigned int i = 0; i < cur_pts.size(); i++)
    {
        Eigen::Vector2d a(cur_pts[i].x, cur_pts[i].y);
        Eigen::Vector3d b;
        m_camera->liftProjective(a, b);
        cur_un_pts.push_back(cv::Point2f(b.x() / b.z(), b.y() / b.z()));
        cur_un_pts_map.insert(make_pair(ids[i], cv::Point2f(b.x() / b.z(), b.y() / b.z())));
        //printf("cur pts id %d %f %f", ids[i], cur_un_pts[i].x, cur_un_pts[i].y);
    }
    // caculate points velocity
    if (!prev_un_pts_map.empty())
    {
        double dt = cur_time - prev_time;
        pts_velocity.clear();
        for (unsigned int i = 0; i < cur_un_pts.size(); i++)
        {
            if (ids[i] != -1)
            {
                std::map<int, cv::Point2f>::iterator it;
                it = prev_un_pts_map.find(ids[i]);
                if (it != prev_un_pts_map.end())
                {
                    double v_x = (cur_un_pts[i].x - it->second.x) / dt;
                    double v_y = (cur_un_pts[i].y - it->second.y) / dt;
                    pts_velocity.push_back(cv::Point2f(v_x, v_y));
                }
                else
                    pts_velocity.push_back(cv::Point2f(0, 0));
            }
            else
            {
                pts_velocity.push_back(cv::Point2f(0, 0));
            }
        }
    }
    else
    {
        for (unsigned int i = 0; i < cur_pts.size(); i++)
        {
            pts_velocity.push_back(cv::Point2f(0, 0));
        }
    }
    prev_un_pts_map = cur_un_pts_map;
}
