
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版提供自定义无效词/关键词百分比显示等功能，请加微信WS908239
#代码升级中，高级版请点击https://market.m.taobao.com/app/idleFish-F2e/widle-taobao-rax/page-detail?wh_weex=true&wx_navbar_transparent=true&id=626524908384&ut_sk=1.XKMInYF2vWUDAH0%252BCExcTiiG_21407387_1599827381403.Copy.detail.626524908384.211196027&forceFlush=1

# coding=UTF-8

import csv
import jieba
import sys
import re
import math
import string
import time
import datetime
import xmind

words={}

dict_seg={}
orig_words=[]
flag=[0]*510000

sqrt_vec_sums={}

set_root_words={}#sentense has how many words

set_words_sentense={}

ind_word_sentense={}

root_words_cnt={}


shit_words={'怎么','把','的','出来','变成','变','做成','做','要','怎么办','教师资格证','教师','资格证',' ','了','什么','上','是','里','小','吗','在'}


def splitAndRecord(text,text_ind):
	seg = jieba.lcut(text)
	uniq_seg=list(set(seg))

	uniq_seg_edump=[]

	for word_in_seg in uniq_seg:
		found=0
		for word_in_shit in shit_words:
			if word_in_seg == word_in_shit:
				found=1
		if found == 0:
			uniq_seg_edump.append(word_in_seg)

	uniq_seg = uniq_seg_edump

	dict_seg[text]=uniq_seg
	sqrt_vec_sums[text]=math.sqrt(len(uniq_seg))

	set_root_words[text]={}


	for i in range(0,len(uniq_seg)):
		if uniq_seg[i] not in ind_word_sentense:
			ind_word_sentense[uniq_seg[i]]=[]
		ind_word_sentense[uniq_seg[i]].append(text_ind)

		#ci pin
		if uniq_seg[i] not in root_words_cnt:
			root_words_cnt[uniq_seg[i]]=0
		root_words_cnt[uniq_seg[i]] +=1


	for i in range(0,len(seg)):

		set_root_words[text][seg[i]]=1



def extract_top(ids,ignored_words):


	#above 80%

	word_ids={}
	total_cnt=0

	for word_id in ids:
		for sentense in dict_seg[orig_words[word_id]]:
			if sentense not in word_ids:
				 word_ids[sentense]=[]
			word_ids[sentense].append(word_id)

	sentense_cnt={}



	#for i in range(0,len(ans_ids)):
	#	print("###########" + ans_words[i])
	#	for ids in ans_ids[i]:
	#		print(orig_words[ids])
		#print(ans_ids[i])

	return ans_words,ans_ids

	#words=['aaa','bbb','ccc','ddd']

	#ans=[[1,2,3,4,5],[7,8,9]]

def print_sheet(par_topic,ids,ignored_words,max_depth,depth):


	[ans_words,ans_ids] = extract_top(ids, ignored_words)

	cnt = 0
	if depth==max_depth:
		for id_word in ids:
			sub_topic = par_topic.addSubTopic()
			sub_topic.setTitle('\n'+orig_words[id_word])
			cnt += 1
			if cnt > 10:
				break
		return




	for word in ans_words:
		ignored_words.append(word)

	print(ans_words,ans_ids)

	index=0
	for word in ans_words:
		sub_topic = par_topic.addSubTopic()
		sub_topic.setTitle('\n'+word)
		print_sheet(sub_topic,ans_ids[index],ignored_words,max_depth,depth+1)
		index = index+1

	return []


start = datetime.datetime.now()

punc=set(",./;'?&-)(+")

with open(sys.argv[1], 'rU',encoding="gbk") as f:
	reader = csv.reader(f)
	print(type(reader))
	i=0
	for row in reader:
		if len(row) > 0:
			#print(row[0].decode('gbk'))
			if len(row[0]) > 40 or len(row[0]) == 0:
				continue
			text = ''.join(c for c in row[0] if not c in punc)
			#print(text.decode('gbk'))
			orig_words.append(text)
			#print(len(orig_words),i)
			splitAndRecord(text,i)
			print(i)
			i=i+1	
		if i > 20004:
			break



print(len(orig_words))
print(len(dict_seg))



sorted_words = sorted(root_words_cnt.items(), key = lambda kv:(kv[1], kv[0]),reverse=True)


#print(sorted_words[0][0])
workbook = xmind.load(sys.argv[1] +'.xmind')

sheet = workbook.createSheet()
sheet.setTitle(sorted_words[0][0])
# 新建一个主题
root_topic = sheet.getRootTopic()
root_topic.setTitle(sorted_words[0][0])


print_sheet(root_topic,list(range(1,len(orig_words))),[sorted_words[0][0]],2,1)

xmind.save(workbook)



