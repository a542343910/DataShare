#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，如需提供服务，请加微信WS908239
#代码升级中，升级版请点击https://market.m.taobao.com/app/idleFish-F2e/widle-taobao-rax/page-detail?wh_weex=true&wx_navbar_transparent=true&id=626524908384&ut_sk=1.XKMInYF2vWUDAH0%252BCExcTiiG_21407387_1599827381403.Copy.detail.626524908384.211196027&forceFlush=1

import csv
import jieba
import sys
import re
import math
import string

words={}

dict_seg={}
orig_words=[]
flag=[0]*510000




def splitAndRecord(text):
	seg = jieba.lcut(text)
	dict_seg[text]=seg
	for i in range(0,len(seg)):
		#print(seg[i])
		if words.has_key(seg[i]):
			words[seg[i]] = words[seg[i]] + 1
		else:
			words[seg[i]] = 1


def cosine(i,j):
	set_words=list(set(dict_seg[orig_words[i]]).union(set(dict_seg[orig_words[j]])))

	vec_i,vec_j = [],[]

	#print("debug:")
	#print(orig_words[i])
	#print(orig_words[j])


	return (imultj / ((math.sqrt(vec_i_sums) * math.sqrt(vec_j_sums))+0.00001))




reload(sys)
punc=set(",./;'?&-)(+")
sys.setdefaultencoding('utf8')
with open('zenmeba.csv', 'rU') as f:
	reader = csv.reader(f)
	print(type(reader))
	i=0
	for row in reader:
		if len(row) > 0:
			print(row[0].decode('gbk'))
			if len(row[0]) > 40 or len(row[0]) == 0:
				continue
			text = ''.join(c for c in row[0] if not c in punc)
			print(text.decode('gbk'))
			orig_words.append(text.decode('gbk'))
			splitAndRecord(text.decode('gbk'))
			print(i)
		i=i+1
		#if i > 2000:
		#	break

print(len(orig_words))
print(len(dict_seg))



f = open('zenmeba-classify.csv','w')
writer = csv.writer(f)


for i in range(0,len(orig_words)):
	if flag[i] == 1:
		continue
	print("handling " + str(i))
	samei = []
	for j in range(i+1,len(orig_words)):
		ans = cosine(i,j)
		#print("debug ans:")
		#print(ans)
		if ans >= 0.8:
			flag[j] = 1
			samei.append(orig_words[j])
	if len(samei) == 0:
		continue
	writer.writerow(['found new topic:' + str(len(samei))])
	writer.writerow(["||||||||"+orig_words[i]])
	for sentense in samei:
		writer.writerow(['||||||||'+sentense])
	writer.writerow(['=================================='])
	writer.writerow('')
	writer.writerow('')


#for key,value in words.items():
#    print('{key}:{value}'.format(key = key, value = value))




#f = open('zenmeba-root-words.csv','w')
#writer = csv.writer(f)

#sorted_words = sorted(words.items(), key = lambda kv:(kv[1], kv[0]))     
#for i in range(0,len(sorted_words)):
#	print(list(sorted_words[i])[0].decode('utf-8'))
#	print(list(sorted_words[i])[1])
#	writer.writerow(list(sorted_words[len(sorted_words) - i -1]))

#f.close()
