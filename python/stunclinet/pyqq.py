#! python
#coding:utf-8

import httplib
import re

class PyQQException(Exception):
	pass

class PyQQ:
	def __init__(self):
		self.httpBody = ''
		self.sid = None

	def Request(self,method,url,data={})
		try:
			_url = httplib.urlsplit(url)
			_server,_port = _url.netloc.split(':')
			if _port is None:
				_port = 80
			_conn = httplib.HTTPConnection(_server,_port,True,3)
			_conn.connect()
			data = urllib.urlencode(data)
			if method == 'get':
				_conn.putrequest("GET",url,None)
				_conn.putheader("Content-Length",'0')
			elif method == 'post':
				_conn.putrequest("POST",url)
				_conn.putheader("Content-Length", str(len(data)))
				_conn.putheader("Content-Type", "application/x-www-form-urlencoded")
			_conn.putheader("Connection", "close")
			_conn.endheaders()

			if len(data) > 0:
				_conn.send(data)
			f = _conn.getresponse()
			self.httpBody = f.read().encode('gbk')
			f.close()
			_conn.close()
		except:
			self.httpBody = ''
		return self.httpBody

	def GetContent(self,start,end):
		idx = self.httpBody.find(start)
		if idx == -1:
			return None
		tmp = self.httpBody.split(start)
		eidx = tmp[1].find(end)
		if eidx == -1:
			return tmp[1][0:]
		else:
			return tmp[1][0:eidx]

	def GetField(self,field):
		keystart = '<postfield name="' + str(field) + '" value="'
		return self.GetContent(keystart,'"/>')

	def Login(self,user,pwd):
		self.user = user
		self.pwd = pwd
		b1Con = self.httpRequest('post','http://pt.3g.qq.com/handleLogin',{'r':'324525157','qq':self.qq,'pwd':self.pwd,'toQQchat':'true','q_from':'','modifySKey':0,'loginType':1})
		self.sid = self.GetContent('sid=','&')
		if self.sid is None:
			raise PyQQException('can not login %s with %s get %s'%(user,pwd,str(self.httpBody)))
		return 0

	def GetMessage(self,peeruser,content):
		'''
			Function : 
			      @param self      : the PyQQ struct
			      @param peeruser  : peer user pattern ,if '' it will return all
			      @param content   : the pattern for message to filter

			return value:
			      list of messages : contain (user,message)
		'''
		if self.sid is None:
			raise PyQQException('not set sid ok')
		users=[]
		messages=[]
		b1con = self.httpRequest('get','http://q32.3g.qq.com/g/s?aid=nqqchatMain&sid='+self.sid)
		if b1con.find('alt="����"/>(') != -1:
			b2con = self.httpRequest('get','http://q32.3g.qq.com/g/s?sid='+ self.sid + '&aid=nqqChat&saveURL=0&r=1310115753&g_f=1653&on=1')

			_tmpqq=self.GetContent('num" value="','"/>')
			if _tmpqq is None:
				return users ,messages
			_tmpmsg=self.GetContent('saveURL=0">��ʾ</a>)','<input name="msg"')
			# now we should match whether it is the message or qq we met
			if len(peeruser) > 0 or len(content) > 0:
				addto = 0
				userre = None
				conre = None
				if len(peeruser) > 0 :
					userre = re.compile(peeruser)
				if len(content) > 0:
					conre = re.compile(content)
				if userre :
					if userre.match(_tmpqq):
						addto = 1
					else:
						addto = 0
				if conre :
					if conre.match(_tmpmsg):
						addto = 1
					else:
						addto = 0
				# if we have match this ,so add to it
				if addto == 1:
					users.append(_tmpqq)
					messages.append(_tmpmsg)
				
			else:
				users.append(_tmpqq)
				messages.append(_tmpmsg)

		return users,messages

	def SendMsg(self,user,content):
		if self.sid is None:
			raise PyQQException('Not connect right')
		_tmpmsg=unicode(content,'gbk').encode('utf8')
		_data={'sid':self.sid,'on':'1','saveURL':'0','saveURL':'0','u':user,'msg':str(_tmpmsg)}
		s1con = self.httpRequest('post','http://q16.3g.qq.com/g/s?sid='+ self.sid +'&aid=sendmsg&tfor=qq',postData)
		return 0

	def KeepAlive(self):
		if self.sid is None:
			# we do not raise
			raise PyQQException('Not connect right')
		s1con = self.httpRequest('get','http://pt5.3g.qq.com/s?aid=nLogin3gqqbysid&3gqqsid='+self.sid)
		return 0
		

