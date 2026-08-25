#!/usr/bin/env python3
"""douyin_search.py - 抖音关键词搜索命令行工具"""

import argparse
import json
import sys
import time
import requests

# 配置区 - 替换为你自己的参数
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                  "AppleWebKit/537.36 (KHTML, like Gecko) "
                  "Chrome/130.0.0.0 Safari/537.36",
    "Cookie": "enter_pc_once=1; UIFID_TEMP=5f6ded4b81121337e65bf4e4aa708494909b01fc35da8b3888cd6351e6c3339fc99fa30bad85a5dcd4846d7425312bbafc99442fccca190b3adb321841ce2fa59047bb6ba320b8c1fe6ce5dc68c4ef61; passport_csrf_token=6ec918cbe0903a85761ddf741724186c; passport_csrf_token_default=6ec918cbe0903a85761ddf741724186c; bd_ticket_guard_client_web_domain=2; UIFID=5f6ded4b81121337e65bf4e4aa708494909b01fc35da8b3888cd6351e6c3339f2903fe5f49cb5038e434fc6891d384e52149460286c5141e1f89cf81aa9efd5f5c883675cc78faccf42997a5a93737859377e519d93973324b4c1e5a75d8762486a9064cba38c1b01f8b8ea11abd2d25c106124accef20d45d1f1bc338d4210640a83d70324e1586d4d0ab15c85e758aafbb44f7aa4ecb17fbe0e7fdca49be8e; SEARCH_UN_LOGIN_PV_CURR_DAY=%7B%22date%22%3A1786676279664%2C%22count%22%3A1%7D; is_support_rtm_web_ts=1; stream_recommend_feed_params=%22%7B%5C%22cookie_enabled%5C%22%3Atrue%2C%5C%22screen_width%5C%22%3A1920%2C%5C%22screen_height%5C%22%3A1080%2C%5C%22browser_online%5C%22%3Atrue%2C%5C%22cpu_core_num%5C%22%3A16%2C%5C%22device_memory%5C%22%3A8%2C%5C%22downlink%5C%22%3A10%2C%5C%22effective_type%5C%22%3A%5C%224g%5C%22%2C%5C%22round_trip_time%5C%22%3A50%7D%22; is_dash_user=1; strategyABtestKey=%221787381906.582%22; passport_assist_user=CkEgLIt-vs2A_qkghXmsJuUU94as_1p-4gW8o1AMSqEQVGeSNQgldQ1cqMEOOoivS8y7jGQwYCV4TkSleh-3rzIIjBpKCjwAAAAAAAAAAAAAUM_qn754JeYltLA4oeS9D3bVwKZkz_k0x4HW5VnbLBDgew5wbiVfd1BgdmJZ3xNwyqAQ6puaDhiJr9ZUIAEiAQNo1MVV; n_mh=idmK-v3CYD2mjtvBOoG4Iqob-htfHZksRf_RBQE39Fk; sid_guard=fd14538aeb86e53988ca7283f162eddd%7C1787382000%7C5184000%7CWed%2C+21-Oct-2026+07%3A00%3A00+GMT; uid_tt=313d44eeebca8d0f65d6bf267a562372; uid_tt_ss=313d44eeebca8d0f65d6bf267a562372; sid_tt=fd14538aeb86e53988ca7283f162eddd; sessionid=fd14538aeb86e53988ca7283f162eddd; sessionid_ss=fd14538aeb86e53988ca7283f162eddd; session_tlb_tag=sttt%7C10%7C_RRTiuuG5TmIynKD8WLt3f_________w3e05ukiEFwl9_pGfAuwmyQ08VfJXEvazWEmyFtH6z0Q%3D; is_staff_user=false; has_biz_token=false; sid_ucp_v1=1.0.0-KGQ4ZWY0Zjk0MmFmNGUwODFmYTg2YjQ4MWMwZjU0NzMzNjJlY2Q2OWIKIQiO-IHd6IyGBhDwkaXUBhjvMSAMMNz2oosGOAdA9AdIBBoCbGYiIGZkMTQ1MzhhZWI4NmU1Mzk4OGNhNzI4M2YxNjJlZGRk; ssid_ucp_v1=1.0.0-KGQ4ZWY0Zjk0MmFmNGUwODFmYTg2YjQ4MWMwZjU0NzMzNjJlY2Q2OWIKIQiO-IHd6IyGBhDwkaXUBhjvMSAMMNz2oosGOAdA9AdIBBoCbGYiIGZkMTQ1MzhhZWI4NmU1Mzk4OGNhNzI4M2YxNjJlZGRk; is_dbsc=false; x_tt_token=00fd14538aeb86e53988ca7283f162eddd01b918b415d7f4827c0751793e92b45f067c632727f98be3c3266dd39dc982395093704b91ebf52d64f11fb4efd1d590d93112a1cdca29d3973b4d500fd603bb27d5283afa8569d7ccd74925becf8dc84c7--0a490a20ddca8aa9b450d4f83f8fdfac9de2d4bde8e18cd46bbd5b465d88e215f96852721220e4073e6cac8ce62f2adb19300f45e36012a8d815a1d370eb155180b18e5022c918f6b4d309-3.0.4; bd_ticket_guard_ts_sign_id=ts.2.eacc0a32a8906e0; _bd_ticket_crypt_cookie=009b3dd1704206f22ef3b74d0bf83759; __security_mc_1_s_sdk_sign_data_key_web_protect=9c82e340-4948-8237; __security_mc_1_s_sdk_cert_key=a26811cb-47eb-87cd; __security_mc_1_s_sdk_crypt_sdk=78925ceb-4bbd-9286; __security_server_data_status=1; login_time=1787382001114; SelfTabRedDotControl=%5B%7B%22id%22%3A%227577246595121416202%22%2C%22u%22%3A417%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227598195788841224242%22%2C%22u%22%3A18%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227602139753353316378%22%2C%22u%22%3A215%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227583369120251398207%22%2C%22u%22%3A148%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227656448300627560498%22%2C%22u%22%3A24%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227583703278145013798%22%2C%22u%22%3A721%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227580724159366301734%22%2C%22u%22%3A190%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227371043186988288052%22%2C%22u%22%3A179%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227632717784006264841%22%2C%22u%22%3A259%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227535079256401905699%22%2C%22u%22%3A153%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227519002018548844598%22%2C%22u%22%3A344%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227642604773727799331%22%2C%22u%22%3A122%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227665713857444186118%22%2C%22u%22%3A33%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227572053889210386486%22%2C%22u%22%3A217%2C%22c%22%3A0%7D%2C%7B%22id%22%3A%227623305009924573193%22%2C%22u%22%3A42%2C%22c%22%3A0%7D%5D; ttwid=1%7CVbK5Tbk5zi_fqgjQIRgLSsF8jOBj79tT7k8qsNC8334%7C1787382005%7Cdbb27d6614d3920102d16150ad2caa9035f6ae05c04ec3c29b70cef86f737979; bd_ticket_guard_generate_ticket_time=2026-08-22/15:00:14; volume_info=%7B%22isUserMute%22%3Afalse%2C%22isMute%22%3Afalse%2C%22volume%22%3A0.5%7D; SEARCH_RESULT_LIST_TYPE=%22single%22; FOLLOW_LIVE_POINT_INFO=%22MS4wLjABAAAAgY2jd2n9A66lKxYGJqN9v5ZchPuWTfER0CgGYzMJrlu4YG9091KJPaC4-FCyDsHf%2F1787414400000%2F0%2F0%2F1787382862550%22; FOLLOW_NUMBER_YELLOW_POINT_INFO=%22MS4wLjABAAAAgY2jd2n9A66lKxYGJqN9v5ZchPuWTfER0CgGYzMJrlu4YG9091KJPaC4-FCyDsHf%2F1787414400000%2F0%2F1787382262550%2F0%22; bd_ticket_guard_client_data=eyJiZC10aWNrZXQtZ3VhcmQtdmVyc2lvbiI6MiwiYmQtdGlja2V0LWd1YXJkLWl0ZXJhdGlvbi12ZXJzaW9uIjoxLCJiZC10aWNrZXQtZ3VhcmQtcmVlLXB1YmxpYy1rZXkiOiJCQVdZVEhmd0tzaW1IZ2wweDlnS1BzL3RraDBBQ0JGL3RzbkJyQzROdU1wMnJvZkh2c283MDY4NFRtODdPL1BYdFhaRzlaK2xOYStJSnVhWnFWWjZhZ1U9IiwiYmQtdGlja2V0LWd1YXJkLXdlYi12ZXJzaW9uIjoyfQ%3D%3D; home_can_add_dy_2_desktop=%221%22; biz_trace_id=e6d112fd; gulu_source_res=eyJwX2luIjoiYzdjYWY4M2UxOGJkNzJkMjFmZDFiOWY5ODgyMGRlN2NkZTkyY2RiZTYzOWExM2U2MjNiNzFhZTlkYjY1NDI4MiJ9; bd_ticket_guard_client_data_v2=eyJyZWVfcHVibGljX2tleSI6IkJBV1lUSGZ3S3NpbUhnbDB4OWdLUHMvdGtoMEFDQkYvdHNuQnJDNE51TXAycm9mSHZzbzcwNjg0VG04N08vUFh0WFpHOVorbE5hK0lKdWFacVZaNmFnVT0iLCJ0c19zaWduIjoidHMuMi5lYWNjMGEzMmE4OTA2ZTAzODMyODJhNjZlNjYwNmI4NWZjZDIxNGI2YzRkYjc3ZjNjZjQxZTQ5NDVhOWQ0N2U5YzRmYmU4N2QyMzE5Y2YwNTMxODYyNGNlZGExNDkxMWNhNDA2ZGVkYmViZWRkYjJlMzBmY2U4ZDRmYTAyNTc1ZCIsInJlcV9jb250ZW50Ijoic2VjX3RzIiwicmVxX3NpZ24iOiJudVdwaG45cnVXSnFCU0dpWm9zaUpCYWlqR2NSZDNGKys1MXdLcTRwNlQwPSIsInNlY190cyI6IiN4N3VDNzJESEczd3l1VEF4L1BxdHFhdjhEdm0vRTRKN2xEbmlXZHNaRWNtcDRiM3p4WHhuWHZRbjgrY3MifQ%3D%3D; publish_badge_show_info=%220%2C0%2C0%2C1787382273423%22; odin_tt=ada0342390e6a231433315bfd331d9d62c594b934c1d53d5caf99e72128149ce5beb8cf4d867ebe89206c18f53cf0a0783c96287067f090b786a1ed005dab318; IsDouyinActive=true; sdk_source_info=7e276470716a68645a606960273f276364697660272927676c715a6d6069756077273f2771777060272927666d776a68605a607d71606b766c6a6b5a7666776c7571273f275e58272927666a6b766a69605a696c6061273f27636469766027292762696a6764695a7364776c6467696076273f275e582729277672715a646971273f2763646976602729277f6b5a666475273f2763646976602729276d6a6e5a6b6a716c273f2763646976602729276c6b6f5a7f6367273f27636469766027292771273f27363d333c3530373d36323d3234272927676c715a75776a716a666a69273f2763646976602778; bit_env=OFRJQZYHkCrXnXBWaD_r-3W9OKGxJL0wMDw2rI1UgmdnlgFPW-p-Y1U5j7c5KQYikq-uQa8KDWUFDybJDSN9QFzqci1F0Lw7Rr3FFD5wG1j43Eq8m8lv5SwI083UHPz4uqzUSwnyWIqQFGg-inE60Y99R090lbwYLyaPZnnvdP-__XIN2NeQ9xWM-KiAokQuoc3JMv8D5L1OUOk4n4pbcBygyOwvvE1DtWSpeg17fVml1uGsS_2UknH395-BmAXMDKSoC2-jVO9TPeN2Aj65sdzwslURWsgpi5I2sorr47blN8h0rTMRS3OkwUwFM4vuUTf0JkIbpYhRvVOSoBT3YhwItSb09O3ZKjY43TyejUtJ54hjORm33c_dMDP54JFqGwq9aRhOshFofxys6i_jxl9zm5WJxX7n_HThyFfbboLP1WGyHqGo1FmSsYRySP9SVVf6ADUe9fvJwX8Vf1WCw-kwy_ezDb4Zg16UszQ2DPGF75kEzfECzE_ayveIh7qfLlmg7Y70kuewyZ8osiRPtNjP--lIhwkufx_46kHxR9A%3D; passport_auth_mix_state=9zl1to3jx7n09lezp3nlf8rnfnz4lox4efn38fpjalp92fn5",
    "Referer": "https://www.douyin.com/",
    "Accept": "application/json, text/plain, */*"
}

SEARCH_URL = "https://www.douyin.com/aweme/v1/web/search/item/"


def search(keyword, count=10, sort_type=0):
    """搜索抖音视频"""
    params = {
        "keyword": keyword,
        "count": count,
        "cursor": 0,
        "search_source": "normal",
        "sort_type": sort_type,  # 0=综合 1=最多点赞 2=最新发布
        "aid": "6383",
        "channel": "douyin_web"
    }
    try:
        resp = requests.get(SEARCH_URL, headers=HEADERS,
                            params=params, timeout=10)
        resp.raise_for_status()
        data = resp.json()

        if "data" not in data or not data["data"]:
            print("未找到相关结果")
            return []

        results = []
        for item in data["data"]:
            video = {
                "title": item.get("desc", "无标题"),
                "author": item.get("author", {}).get("nickname", ""),
                "likes": item.get("statistics", {}).get("digg_count", 0),
                "comments": item.get("statistics", {}).get("comment_count", 0),
                "shares": item.get("statistics", {}).get("share_count", 0),
                "url": f"https://www.douyin.com/video/{item.get('aweme_id', '')}"
            }
            results.append(video)
        return results

    except Exception as e:
        print(f"请求失败: {e}", file=sys.stderr)
        return []


def format_output(results, fmt="table"):
    """格式化输出"""
    if fmt == "json":
        print(json.dumps(results, ensure_ascii=False, indent=2))
        return

    # 表格形式输出
    for i, r in enumerate(results, 1):
        print(f"\n{'='*60}")
        print(f"[{i}] {r['title']}")
        print(f"    作者: {r['author']}")
        print(f"    👍 {r['likes']}  💬 {r['comments']}  "
              f"🔄 {r['shares']}")
        print(f"    🔗 {r['url']}")


def main():
    parser = argparse.ArgumentParser(
        description="抖音关键词搜索命令行工具")
    parser.add_argument("keyword", help="搜索关键词")
    parser.add_argument("-n", "--count", type=int, default=10,
                        help="返回结果数量 (默认10)")
    parser.add_argument("-s", "--sort", type=int, default=0,
                        choices=[0, 1, 2],
                        help="排序: 0=综合 1=最多点赞 2=最新")
    parser.add_argument("-f", "--format", default="table",
                        choices=["table", "json"],
                        help="输出格式 (默认table)")
    args = parser.parse_args()

    results = search(args.keyword, args.count, args.sort)
    if results:
        format_output(results, args.format)


if __name__ == "__main__":
    main()
