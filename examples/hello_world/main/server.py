from flask import Flask, request, jsonify, send_file
import time

app = Flask(__name__)

@app.route('/upload', methods=['POST'])
def upload_audio():
    audio_data = request.data  # 获取原始的二进制音频数据
    print(f"Received audio data of length: {len(audio_data)}")
    
    if len(audio_data) == 0:
        return "No audio data received", 400

    # 可以选择将音频数据写入文件，便于进一步验证
    with open("received_audio.wav", "wb") as f:
        f.write(audio_data)

    return "Audio received", 200

@app.route('/get_response', methods=['GET'])
def get_response():
    # 第一个字符串返回
    time.sleep(2)  # 模拟处理
    return "First string response", 200

@app.route('/get_response2', methods=['GET'])
def get_response2():
    # 第二个长字符串返回
    time.sleep(3)  # 模拟处理
    return "Second long string response,Second long string response,Second long strinSecSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseSecond long string responseond long string responseg response,Second long string response", 200

@app.route('/get_wav', methods=['GET'])
def get_wav():
    # 返回WAV格式的文件
    return send_file('demo.wav', mimetype='audio/wav')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5001)
