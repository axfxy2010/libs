#coding:utf-8
#����python2.6�汾����
import httplib,urllib,os,threading,re
import sys 
reload(sys) 
sys.setdefaultencoding('utf8') 
class PYQQ:
    def __init__(self):
        self.reqIndex = 0
    
    #HTTP����
    def httpRequest(self,method,url,data={}):
        try:
            _urld = httplib.urlsplit(url)
            conn = httplib.HTTPConnection(_urld.netloc,80,True,3)
            conn.connect()
            data = urllib.urlencode(data)
            if method=='get':
                conn.putrequest("GET", url, None)
                conn.putheader("Content-Length",'0')
            elif method=='post':
                conn.putrequest("POST", url)
                conn.putheader("Content-Length", str(len(data)))
                conn.putheader("Content-Type", "application/x-www-form-urlencoded")
            
            conn.putheader("Connection", "close")
            conn.endheaders()
            
            if len(data)>0:
                conn.send(data)
            f = conn.getresponse()
            self.httpBody = f.read().encode('gbk')
            f.close()
            conn.close()
        except:
            self.httpBody=''
        return self.httpBody
    #HTTP�����pycurl�汾��������ĳ���ѡһ����
    def httpRequest_(self,method,url,data={}):
        import pycurl,StringIO
        c = pycurl.Curl()
        c.setopt(pycurl.URL,url)
        if method=='post':
            import urllib
            c.setopt(c.POSTFIELDS, urllib.urlencode(data))
        
        c.fp = StringIO.StringIO()
        c.setopt(pycurl.WRITEFUNCTION,c.fp.write)
        c.perform()
        self.httpBody = c.fp.getvalue().encode('gbk')
        del c.fp
        c.close()
        c = None
        return self.httpBody
    #ͨ����β��ȡ�ַ���������
    def getCon(self,start,end):
        findex = self.httpBody.find(start)
        if findex == -1 : return None
        tmp = self.httpBody.split(start)
        
        eindex = tmp[1].find(end)
        if eindex == -1:
            return tmp[1][0:]
        else:
            return tmp[1][0:eindex]
    #��ȡpostfield��ֵ
    def getField(self,fd):
        KeyStart = '<postfield name="'+ str(fd) +'" value="'
        return self.getCon(KeyStart,'"/>')
    #��ȡ��½��֤��,����������ǰĿ¼��qqcode.gif
    def getSafecode(self):
        url = self.getCon('<img src="','"')
        import urllib2
        pager = urllib2.urlopen(url)
        data=pager.read()
        file=open(os.getcwd()+'\qqcode.gif','w+b')
        file.write(data)
        file.close()
        return True
    #��½QQ
    def login(self):
        self.qq = raw_input('������QQ��:')
        self.pwd = raw_input('����������:')
        s1Back = self.httpRequest('post','http://pt.3g.qq.com/handleLogin',{'r':'324525157','qq':self.qq,'pwd':self.pwd,'toQQchat':'true','q_from':'','modifySKey':0,'loginType':1})
        if s1Back.find('��������֤��')!=-1:
            self.sid = self.getField('sid')
            self.hexpwd = self.getField('hexpwd')
            self.extend = self.getField('extend')
            self.r_sid = self.getField('r_sid')
            self.rip = self.getField('rip')
            if self.getSafecode():
                self.safeCode = raw_input('��������֤�루���ļ�ͬĿ¼��qqcode.gif��:')
            else:
                print '��֤����ش���'
            
            postData = {'sid':self.sid,'qq':self.qq,'hexpwd':self.hexpwd,'hexp':'true','auto':'0',
                        'logintitle':'�ֻ���Ѷ��','q_from':'','modifySKey':'0','q_status':'10',
                        'r':'271','loginType':'1','prev_url':'10','extend':self.extend,'r_sid':self.r_sid,
                        'bid_code':'','bid':'-1','toQQchat':'true','rip':self.rip,'verify':self.safeCode,
            }
            s1Back = self.httpRequest('post','http://pt.3g.qq.com/handleLogin',postData)
        
        self.sid = self.getCon('sid=','&')
        print '��½�ɹ�'
        self.getMsgFun()    
    #��ʱ��ȡ��Ϣ
    def getMsgFun(self):
        self.reqIndex = self.reqIndex + 1
        s2Back = self.httpRequest('get','http://q32.3g.qq.com/g/s?aid=nqqchatMain&sid='+self.sid)
        if s2Back.find('alt="����"/>(')!=-1:
            #������Ϣ�������ȡ��Ϣҳ��
            s3back = self.httpRequest('get','http://q32.3g.qq.com/g/s?sid='+ self.sid + '&aid=nqqChat&saveURL=0&r=1310115753&g_f=1653&on=1')
            
            #��Ϣ�����ߵ��ǳ�
            if s3back.find('title="��ʱ�Ự')!=-1:
                _fromName = '��ʱ�Ի�'
            else:
                _fromName = self.getCon('title="��','����')
            
            #��Ϣ�����ߵ�QQ��
            _fromQQ = self.getCon('num" value="','"/>') 
            
            #��Ϣ����
            _msg_tmp = self.getCon('saveURL=0">��ʾ</a>)','<input name="msg"')
            crlf = '\n'
            if _msg_tmp.find('\r\n')!=-1: crlf = '\r\n'
            _msg = re.findall(r'(.+)<br/>'+ crlf +'(.+)<br/>',_msg_tmp)
            
            for _data in _msg:
                self.getMsg({'qq':_fromQQ,'nick':_fromName,'time':_data[0],'msg':str(_data[1]).strip()})
        
        if self.reqIndex>=30:
            #��������
            _url = 'http://pt5.3g.qq.com/s?aid=nLogin3gqqbysid&3gqqsid='+self.sid
            self.httpRequest('get',_url)
            self.reqIndex = 0
        t = threading.Timer(2.0,self.getMsgFun)
        t.start()    
    #������Ϣ
    #qq Ŀ��QQ
    #msg ��������
    def sendMsgFun(self,qq,msg):
        msg = unicode(msg,'gbk').encode('utf8')
        postData = {'sid':self.sid,'on':'1','saveURL':'0','saveURL':'0','u':qq,'msg':str(msg),}
        s1Back = self.httpRequest('post','http://q16.3g.qq.com/g/s?sid='+ self.sid +'&aid=sendmsg&tfor=qq',postData)
        print '������Ϣ��',qq,'�ɹ�'    
    #�յ���Ϣ�Ľӿڣ����ػ���д�÷���
    def getMsg(self,data):
        print data['time'],"�յ�",data['nick'],"(",data['qq'],")������Ϣ"
        self.sendMsgFun(data['qq'],data['nick']+'�����յ��������Ϣ��'+ data['msg'])
QQ = PYQQ()
QQ.login()
